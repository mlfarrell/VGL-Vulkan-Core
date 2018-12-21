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

#pragma once

#include "vulkan.h"
#include "VulkanSwapChain.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"
#include <vector>

#ifdef VGL_VULKAN_CORE_STANDALONE
#define VGLINLINE inline
#endif

namespace vgl
{
  namespace core
  {
    class VulkanInstance
    {
    public:
      VulkanInstance();
      ~VulkanInstance();

      VulkanInstance(const VulkanInstance &rhs) = delete;
      VulkanInstance(const VulkanInstance &&rhs) = delete;
      VulkanInstance &operator =(const VulkanInstance &rhs) = delete;

      static VulkanInstance &currentInstance();

      inline VkInstance get() { return instance; }
      inline VkDevice getDefaultDevice() { return device; }
      inline VkPhysicalDevice getPhysicalDevice() { return physicalDevice; }
      inline VkPhysicalDeviceProperties getPhysicalDeviceProperties() { return physicalDeviceProperties; }
      inline int getGraphicsQueueFamily() { return graphicsQueueFamily; }
      inline VkQueue getGraphicsQueue() { return graphicsQueue; }
      inline VkPipelineCache getPipelineCache() { return pipelineCache; }

      inline VulkanMemoryManager *getMemoryManager() { return memoryManager; }
      inline VulkanAsyncResourceMonitor *getResourceMonitor() { return resourceMonitor; }

      inline VulkanSwapChain *getSwapChain() { return swapChain; }

      //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      //Down the road, if I decide to do multiple threads building command buffers, I'll break these methods into a 
      //"sub instance" type that will live in each thread and provide access to the current building command lists
      inline VkCommandPool getTransferCommandPool() { return transferCommandPool; }
      inline std::pair<VkCommandBuffer, VkFence> getCurrentTransferCommandBuffer() { return currentTransferCommandBuffer; }
      std::pair<VkCommandBuffer, VkFence> beginTransferCommands();
      void endTransferCommands();

      ///Unlike the getCurrentTransferCommandBuffer method, this one will start a new transfer command buffer if needed 
      /// (requiring a call to endTransferCommands() in the future)
      std::pair<VkCommandBuffer, VkFence> getTransferCommandBuffer();

      inline void setCurrentRenderingCommandBuffer(VkCommandBuffer commandBuffer) { currentRenderingCommandBuffer = commandBuffer; }
      VGLINLINE VkCommandBuffer getCurrentRenderingCommandBuffer() { return currentRenderingCommandBuffer; }
      //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      void waitForDeviceIdle();

      std::string getDeviceDescription();

    protected:
      VkInstance instance;
      VkDevice device;
      VkPhysicalDevice physicalDevice;
      VkQueue graphicsQueue;
      VkCommandPool transferCommandPool;
      std::pair<VkCommandBuffer, VkFence> currentTransferCommandBuffer = { VK_NULL_HANDLE, VK_NULL_HANDLE };
      VkCommandBuffer currentRenderingCommandBuffer = VK_NULL_HANDLE;
      VkPipelineCache pipelineCache;

      VkPhysicalDeviceProperties physicalDeviceProperties;
      VkPhysicalDeviceFeatures physicalDeviceFeatures;

      VulkanSwapChain *swapChain = nullptr;
      VulkanMemoryManager *memoryManager = nullptr;
      VulkanAsyncResourceMonitor *resourceMonitor = nullptr;

      int graphicsQueueFamily = -1;

      void getRequiredInstanceExtensions();
      void getRequiredDeviceExtensions();
      void setupDefaultDevice();
      void setupLogicalDevice();
      void setupSwapChain();
      void setupPipelineCache();

      bool checkValidationLayers();
      int findQueueFamilies(VkPhysicalDevice device);

      std::vector<const char *> validationLayers;
      std::vector<const char *> instanceExtensions;
      std::vector<const char *> requiredInstanceExtensions, requiredDeviceExtensions;
      VkDebugReportCallbackEXT msgCallback = nullptr;

      bool validationEnabled = false;
    };
  }
}