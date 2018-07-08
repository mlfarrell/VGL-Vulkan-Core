#pragma once

#include <vector>
#include "vulkan.h"

namespace vgl
{
  namespace core
  {
    class VulkanInstance;

    class VulkanSwapChainBase
    {
    public:
      VulkanSwapChainBase(VulkanInstance *instance);
      ~VulkanSwapChainBase();

      uint32_t acquireNextImage();
      void submitAndPresent(VkCommandBuffer commandBuffer, uint32_t imageIndex);

      inline VulkanInstance *getInstance() { return instance; }
      inline VkDevice getDevice() { return swapchainDevice; }
      inline const std::vector<VkImage> &getImages() { return swapChainImages; }
      inline VkFormat getImageFormat() { return swapChainImageFormat; }
      inline VkExtent2D getExtent() { return swapChainExtent; }

    protected:
      static const int MAX_FRAMES_IN_FLIGHT = 2;
      int currentFrame = 0;

      VulkanInstance *instance;
      VkSwapchainKHR swapChain;
      VkSurfaceKHR surface;
      VkQueue graphicsQueue, presentQueue;
      VkDevice swapchainDevice;

      VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
      VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
      VkFence frameFences[MAX_FRAMES_IN_FLIGHT];
      uint64_t frameIds[MAX_FRAMES_IN_FLIGHT];

      std::vector<VkImage> swapChainImages;
      VkFormat swapChainImageFormat;
      VkExtent2D swapChainExtent;

      uint32_t winW = 0, winH = 0;

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
      void obtainSwapchainImages();
      void createSyncPrimitives();
    };
  }
}
