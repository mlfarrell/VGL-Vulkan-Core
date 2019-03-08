/*********************************************************************
Copyright 2018 VERTO STUDIO LLC.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
***************************************************************************/

#include "pch.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include "VulkanInstance.h"
#include "VulkanExtensionLoader.h"
#include "VulkanSwapChain.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "FileManager.h"
#endif

//#define FORCE_VALIDATION 1

using namespace std;

namespace vgl
{
  namespace core
  {
    static VulkanInstance *currentVulkanInstance = nullptr;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char *layerPrefix, const char *msg, void *userData);

    VulkanInstance &VulkanInstance::currentInstance()
    {
      return *currentVulkanInstance;
    }

    VulkanInstance::VulkanInstance()
    {
#if (defined DEBUG || defined FORCE_VALIDATION) && (!defined MACOSX) //validation doesn't work on apple yet
      validationEnabled = true;

      validationLayers.push_back("VK_LAYER_LUNARG_standard_validation");
      validationLayers.push_back("VK_LAYER_LUNARG_parameter_validation");
      validationLayers.push_back("VK_LAYER_LUNARG_object_tracker");
#endif

      if(validationEnabled && !checkValidationLayers()) 
      {
        throw vgl_runtime_error("Vulkan validation layers requested, but not available!");
      }

      VkApplicationInfo appInfo = {};
      appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      appInfo.pApplicationName = "VGL (Verto Studio Graphics Engine)";
      appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
      appInfo.pEngineName = "VGL (Verto Studio Graphics Engine)";
      appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
      appInfo.apiVersion = VK_API_VERSION_1_0;

      VkInstanceCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      createInfo.pApplicationInfo = &appInfo;      
      
      getRequiredInstanceExtensions();

      //append optional extensions
      if(validationEnabled)
        instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
      copy(requiredInstanceExtensions.begin(), requiredInstanceExtensions.end(), back_inserter(instanceExtensions));

      createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
      createInfo.ppEnabledExtensionNames = instanceExtensions.data();

      if(validationEnabled)
      {
        createInfo.enabledLayerCount = (uint32_t)(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
      }
      else
      {
        createInfo.enabledLayerCount = 0;
      }

      if(vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) 
      {
        throw vgl_runtime_error("Failed to create Vulkan instance!");
      }

      uint32_t extensionCount = 0;
      vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
      vector<VkExtensionProperties> extensions(extensionCount);
      vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

      VulkanExtensionLoader::resolveInstanceExtensions(instance);

      if(validationEnabled)
      {
        PFN_vkDebugReportCallbackEXT callback = debugCallback;
        VkDebugReportCallbackCreateInfoEXT createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        createInfo.pfnCallback = debugCallback;

        if(VulkanExtensionLoader::vkCreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &msgCallback) != VK_SUCCESS) 
        {
          throw vgl_runtime_error("failed to set up debug callback!");
        }
      }

      currentVulkanInstance = this;

      getRequiredDeviceExtensions();
      setupDefaultDevice();
    }

    void VulkanInstance::getRequiredInstanceExtensions()
    {
      requiredInstanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef WIN32
      requiredInstanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined MACOSX
      requiredInstanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif
    }

    void VulkanInstance::getRequiredDeviceExtensions()
    {
      requiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
      requiredDeviceExtensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
      //requiredDeviceExtensions.push_back(VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
    }

    void VulkanInstance::setupDefaultDevice()
    {
      uint32_t deviceCount = 0;
      vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

      if(deviceCount == 0)
        throw vgl_runtime_error("Failed to find GPUs with Vulkan support!");

      vector<VkPhysicalDevice> devices(deviceCount);
      vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

      auto rateDevice = [=](VkPhysicalDevice device)->int {
        int score = 0;

        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;

        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        //Discrete GPUs have a significant performance advantage
        if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
          score += 1000;

        score += deviceProperties.limits.maxImageDimension2D;
        score += deviceFeatures.samplerAnisotropy ? 1000 : 0;
        score += deviceFeatures.sampleRateShading ? 500 : 0;

        //set score to 0 for deal breakers
        int graphicsFamily = findQueueFamilies(device);
        if(graphicsFamily < 0)
          score = 0;

        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        set<string> requiredExtensions(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());
        for(const auto &extension : availableExtensions) 
        {
          requiredExtensions.erase(extension.extensionName);
        }

        if(!requiredExtensions.empty())
          score = 0;

        return score;
      };

      multimap<int, VkPhysicalDevice> candidates;
      for(const auto &device : devices)
      {
        int score = rateDevice(device);
        candidates.insert({ score, device });
      }

      if(candidates.rbegin()->first > 0)
      {
        physicalDevice = candidates.rbegin()->second;
        graphicsQueueFamily = findQueueFamilies(physicalDevice);
        setupLogicalDevice();

        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);

        vout << "VGL Vulkan Core On: " << physicalDeviceProperties.deviceName << " (" << physicalDeviceProperties.deviceType << ")" << endl;
        vout << "Vulkan Version: " << VK_VERSION_MAJOR(physicalDeviceProperties.apiVersion) << '.' << VK_VERSION_MINOR(physicalDeviceProperties.apiVersion) << endl;

        //init system-wide memory manager
        memoryManager = new VulkanMemoryManager(physicalDevice, device);

        //init system-wide resource monitor
        resourceMonitor = new VulkanAsyncResourceMonitor(memoryManager);

        //create default command pool for transfer commands
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        if(vkCreateCommandPool(device, &poolInfo, nullptr, &transferCommandPool) != VK_SUCCESS)
          throw vgl_runtime_error("Unable to create Vulkan command pool");

        //create main pipeline cache
        setupPipelineCache();

        setupSwapChain();
      }
      else
      {
        throw vgl_runtime_error("Failed to find a suitable GPU for Vulkan graphics core!");
      }
    }

    void VulkanInstance::setupLogicalDevice()
    {
      VkDeviceQueueCreateInfo queueCreateInfo = {};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
      queueCreateInfo.queueCount = 1;

      float queuePriority = 1.0f;
      queueCreateInfo.pQueuePriorities = &queuePriority;

      VkPhysicalDeviceFeatures deviceFeatures = {};
      if(physicalDeviceFeatures.samplerAnisotropy)
        deviceFeatures.samplerAnisotropy = VK_TRUE;
      if(physicalDeviceFeatures.sampleRateShading)
        deviceFeatures.sampleRateShading = VK_TRUE;
#ifndef MACOSX
      if(physicalDeviceFeatures.geometryShader)
        deviceFeatures.geometryShader = VK_TRUE;
#endif

      VkDeviceCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      createInfo.pQueueCreateInfos = &queueCreateInfo;
      createInfo.queueCreateInfoCount = 1;
      createInfo.pEnabledFeatures = &deviceFeatures;

      createInfo.enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size();
      createInfo.ppEnabledExtensionNames = requiredDeviceExtensions.data();

      if(validationEnabled)
      {
        createInfo.enabledLayerCount = (uint32_t)(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
      }
      else 
      {
        createInfo.enabledLayerCount = 0;
      }

      if(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create logical Vulkan device!");
      
      VulkanExtensionLoader::resolveDeviceExtensions(instance, device);

      vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    }

    void VulkanInstance::setupSwapChain()
    {
      swapChain = new VulkanSwapChain(this);
    }

    void VulkanInstance::setupPipelineCache()
    {
#ifndef VGL_VULKAN_CORE_STANDALONE
      ifstream inf(vutil::FileManager::manager().getCacheDirectory() + "/vkPipelineCache.bin", ios::binary);
#else
      ifstream inf("vkPipelineCache.bin", ios::binary);
#endif
      uint8_t *data = nullptr;
      streampos sz;

      if(inf.is_open())
      {
        inf.seekg(0, ios::end);
        sz = inf.tellg();
        if(sz)
        {
          data = new uint8_t[sz];
          inf.seekg(0, ios::beg);
          inf.read((char *)data, sz);
        }
      }

      VkPipelineCacheCreateInfo cacheInfo = {};
      cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      cacheInfo.pInitialData = data;
      cacheInfo.initialDataSize = sz;
      pipelineCache = VK_NULL_HANDLE;

      if(vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) != VK_SUCCESS)
        throw vgl_runtime_error("Unable to create Vulkan pipeline cache");

      if(data)
        delete []data;
    }

    bool VulkanInstance::checkValidationLayers()
    {
      uint32_t layerCount;
      vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
      vector<VkLayerProperties> availableLayers(layerCount);
      vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

      for(auto layerName : validationLayers)
      {
        bool layerFound = false;

        for(const auto &layerProperties : availableLayers) 
        {
          if(strcmp(layerName, layerProperties.layerName) == 0) 
          {
            layerFound = true;
            break;
          }
        }

        if(!layerFound) 
        {
          return false;
        }
      }

      return true;
    }

    int VulkanInstance::findQueueFamilies(VkPhysicalDevice device)
    {
      int result = -1;
      uint32_t queueFamilyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
      std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

      int i = 0;
      for(const auto &queueFamily : queueFamilies) 
      {
        if(queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
          result = i;

        if(result >= 0)
          break;
        i++;
      }

      return result;
    }

    VulkanInstance::~VulkanInstance()
    {
      if(validationEnabled)
        VulkanExtensionLoader::vkDestroyDebugReportCallbackEXT(instance, msgCallback, nullptr);      

      if(device)
        vkDeviceWaitIdle(device);

      if(swapChain)
        delete swapChain;

      if(pipelineCache)
      {
        size_t sz;
        if(vkGetPipelineCacheData(device, pipelineCache, &sz, nullptr) == VK_SUCCESS)
        {
          uint8_t *data = new uint8_t[sz];

          if(vkGetPipelineCacheData(device, pipelineCache, &sz, data) == VK_SUCCESS)
          {
#ifndef VGL_VULKAN_CORE_STANDALONE
            ofstream outf(vutil::FileManager::manager().getCacheDirectory() + "/vkPipelineCache.bin", ios::binary);
#else
            ofstream outf("vkPipelineCache.bin", ios::binary);
#endif
            outf.write((char *)data, sz);
          }

          delete []data;
          vkDestroyPipelineCache(device, pipelineCache, nullptr);
        }
      }

      if(resourceMonitor)
        delete resourceMonitor;

      if(memoryManager)
        delete memoryManager;

      if(transferCommandPool)
        vkDestroyCommandPool(device, transferCommandPool, nullptr);

      if(device)
      {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
      }
      if(instance)
        vkDestroyInstance(instance, nullptr);
    }

    pair<VkCommandBuffer, VkFence> VulkanInstance::getTransferCommandBuffer()
    {
      if(currentTransferCommandBuffer.first)
        return currentTransferCommandBuffer;
      else
        return beginTransferCommands();
    }

    pair<VkCommandBuffer, VkFence> VulkanInstance::beginTransferCommands()
    {
#ifdef DEBUG
      if(currentTransferCommandBuffer.first)
        throw vgl_runtime_error("Orphaned transfer command buffer!");
#endif

      VkCommandBufferAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocInfo.commandPool = transferCommandPool;
      allocInfo.commandBufferCount = 1;

      VkCommandBuffer commandBuffer;
      vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

      VkCommandBufferBeginInfo beginInfo = {};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(commandBuffer, &beginInfo);

      VkFence transferFence;
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

      if(vkCreateFence(device, &fenceInfo, nullptr, &transferFence) != VK_SUCCESS)
        throw vgl_runtime_error("Could not create Vulkan Fence!");

      currentTransferCommandBuffer = { commandBuffer, transferFence };
      return currentTransferCommandBuffer;
    }

    void VulkanInstance::endTransferCommands()
    {
      auto transferFence = currentTransferCommandBuffer.second;

      vkEndCommandBuffer(currentTransferCommandBuffer.first);

      VkSubmitInfo submitInfo = {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &currentTransferCommandBuffer.first;

      vkQueueSubmit(graphicsQueue, 1, &submitInfo, transferFence);

      auto fenceHandle = VulkanAsyncResourceHandle::newFence(resourceMonitor, device, transferFence);
      auto cmdBufHandle = VulkanAsyncResourceHandle::newCommandBuffer(resourceMonitor, device, currentTransferCommandBuffer.first, transferCommandPool);
      VulkanAsyncResourceCollection transferResources(resourceMonitor, fenceHandle, {
          cmdBufHandle, fenceHandle
        });
      resourceMonitor->append(move(transferResources), true);
      fenceHandle->release();
      cmdBufHandle->release();

      currentTransferCommandBuffer = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    }

    void VulkanInstance::waitForDeviceIdle()
    {
      if(device)
        vkDeviceWaitIdle(device);
    }

    string VulkanInstance::getDeviceDescription()
    {
      return string(physicalDeviceProperties.deviceName);
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
      VkDebugReportFlagsEXT flags,
      VkDebugReportObjectTypeEXT objType,
      uint64_t obj,
      size_t location,
      int32_t code,
      const char *layerPrefix,
      const char *msg,
      void *userData)
    {
      ostringstream logerr;

      if(flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
      {
        logerr << "Vulkan ERROR: [" << layerPrefix << "] Code " << code << " : " << msg << endl;
      }
      else if(flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
      {
        logerr << "Vulkan Warning: [" << layerPrefix << "] Code " << code << " : " << msg << endl;
      }

#ifdef WIN32
      OutputDebugStringA(logerr.str().c_str());
#else
      cerr << logerr.str();
#endif

      return VK_FALSE;
    }
  }
}
