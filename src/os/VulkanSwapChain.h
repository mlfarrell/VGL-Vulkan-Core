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

#include <vector>
#include "vulkan.h"
#include "VulkanSurface.h"

namespace vgl
{
  namespace core
  {
    class VulkanInstance;
    class VulkanTexture;

    class VulkanSwapChain
    {
    public:
      struct InitParameters
      {
        bool mailboxPresentMode, allowReadingImages;
        int depthBits, multiSamples;
      };

      static void setInitParameters(InitParameters params);

      VulkanSwapChain(VulkanInstance *instance, VulkanSurface *surface);
      ~VulkanSwapChain();

      uint32_t acquireNextImage();
      void submitCommands(VkCommandBuffer commandBuffer, bool waitOnImageAcquire=true);
      bool presentImage(uint32_t imageIndex);
      bool submitAndPresent(VkCommandBuffer commandBuffer, uint32_t imageIndex);

      void waitForRender();

      ///Do not call this unless you want to render to a swapchain image without presenting it (skipping acquireNextImage & presentImage)
      /// In that case call waitForPreviousFrame(), render to previous presented image, then call submitCommands(cb, false);
      void waitForPreviousFrame();

      inline VulkanInstance *getInstance() { return instance; }
      inline VkDevice getDevice() { return swapchainDevice; }
      inline const std::vector<VkImage> &getImages() { return swapChainImages; }
      inline int getNumImages() { return (int)swapChainImages.size(); }
      inline VkFormat getImageFormat() { return swapChainImageFormat; }
      inline VkExtent2D getExtent() { return swapChainExtent; }
      inline VulkanTexture *getDepthAttachment() { return depthAttachment; }
      inline VkSampleCountFlagBits getNumMultiSamples() { return multiSamples; }

      inline VulkanTexture *getMSAAColorTarget() { return msaaColorTarget; }
      inline VulkanTexture *getMSAADepthTarget() { return msaaDepthTarget; }

      inline uint64_t getCurrentFrameId() { return frameId; }

    protected:
      static const int MAX_FRAMES_IN_FLIGHT = 2;
      int currentFrame = 0, lastSubmittedFrame = -1;

      static InitParameters initParams;

      VulkanInstance *instance;
      VulkanSurface *surface;
      VkSwapchainKHR swapChain;
      VkQueue graphicsQueue, presentQueue;
      VkDevice swapchainDevice;

      VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
      VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
      VkFence frameFences[MAX_FRAMES_IN_FLIGHT];
      uint64_t frameIds[MAX_FRAMES_IN_FLIGHT];

      std::vector<VkImage> swapChainImages;
      VkFormat swapChainImageFormat;
      VkExtent2D swapChainExtent;
      VkSampleCountFlagBits multiSamples = VK_SAMPLE_COUNT_1_BIT;

      VulkanTexture *depthAttachment = nullptr;
      VulkanTexture *msaaColorTarget = nullptr, *msaaDepthTarget = nullptr;

      uint32_t winW = 0, winH = 0;
      bool defunct = false;

      ///Should be called after base class initializes the surface
      void init();
      void cleanup();

      struct SwapChainSupportDetails 
      {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
      };
      SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

      VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
      VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
      VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

      void createSwapchain();
      void createDepthAttachment();
      void obtainSwapchainImages();
      void createSyncPrimitives();

    private:
      static uint64_t frameId, completedFrameId;
      inline uint64_t getNewFrameId() { return ++frameId; }
      void completeFrame(uint64_t frameId);
    };
  }
}
