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
#include "VulkanSurface.h"
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
      struct VulkanConfig
      {
        bool headless;
      };
      
      VulkanInstance(VulkanConfig config={});
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

      ///Used to handle window resizing
      void recreateSwapChain();

      ///If you intend to destroy the native window before the instance, you'll need to call this
      void destroySwapChain();

      //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      //Down the road, if I decide to do multiple threads building command buffers, I'll break these methods into a 
      //"sub instance" type that will live in each thread and provide access to the current building command lists
      inline VkCommandPool getTransferCommandPool() { return transferCommandPool; }
      inline VkCommandBuffer getCurrentTransferCommandBuffer() { return currentTransferCommandBuffer.first; }
      VkCommandBuffer beginTransferCommands();
      void endTransferCommands(bool wait=false); //flush

      ///Unlike the getCurrentTransferCommandBuffer method, this one will start a new transfer command buffer if needed 
      /// (requiring a call to endTransferCommands() in the future)
      VkCommandBuffer getTransferCommandBuffer();

      inline void setCurrentRenderingCommandBuffer(VkCommandBuffer commandBuffer) { currentRenderingCommandBuffer = commandBuffer; }
      VGLINLINE VkCommandBuffer getCurrentRenderingCommandBuffer() { return currentRenderingCommandBuffer; }
      //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      void waitForDeviceIdle();

      std::string getDeviceDescription();

      ///Use this if you need a reference back to the owning engine renderer class from the instance
      inline void setParentRenderer(void *renderer) { parentRenderer = renderer; }
      inline void *getParentRenderer() { return parentRenderer; }

      ///Find out which instance/device extensions have been enabled for vulkan
      inline const std::vector<const char *> &getEnabledInstanceExtensions() { return instanceExtensions; }
      inline const std::vector<const char *> &getEnabledDeviceExtensions() { return deviceExtensions; }

      ///Useful for temporarily getting around bugs in vulkan validation layers (I use breakpoints to debug these)
      static void enableValidationReports(bool b);
      
    protected:
      void *parentRenderer = nullptr;
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

      VulkanSurface *surface = nullptr;
      VulkanSwapChain *swapChain = nullptr;
      VulkanMemoryManager *memoryManager = nullptr;
      VulkanAsyncResourceMonitor *resourceMonitor = nullptr;
      
      int graphicsQueueFamily = -1;
      VulkanConfig launchConfig;

      void getRequiredInstanceExtensions();
      void getRequiredDeviceExtensions();
      void checkOptionalDeviceExtensions();
      void setupDefaultDevice();
      void setupLogicalDevice();
      void setupSurface();
      void setupSwapChain();
      void setupPipelineCache();

      bool checkValidationLayers();
      int findQueueFamilies(VkPhysicalDevice device);

      std::vector<const char *> validationLayers;
      std::vector<const char *> instanceExtensions;
      std::vector<const char *> deviceExtensions;
      std::vector<const char *> requiredInstanceExtensions, requiredDeviceExtensions, optionalDeviceExtensions;
      VkDebugReportCallbackEXT msgCallback = NULL;

      bool validationEnabled = false;
    };
  }
}
