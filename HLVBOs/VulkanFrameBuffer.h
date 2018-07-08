#pragma once

#include "vulkan.h"
#include "VulkanSwapChain.h"

namespace vgl
{
  class VulkanInstance;

  namespace core
  {
    class VulkanFrameBuffer
    {
    public:
      friend class VulkanSwapChainBase;

      VulkanFrameBuffer(VulkanSwapChain *swapchain);
      ~VulkanFrameBuffer();

      VkCommandBuffer getCommandBuffer(int imageIndex=0);

      inline VkFramebuffer get(int imageIndex=0) { return framebuffers[imageIndex]; }
      inline VkRenderPass getRenderPass() { return renderPass; }

      static inline uint64_t getCurrentFrameId() { return frameId; }
    protected:
      void createRenderPass(VulkanSwapChain *swapchain);

      VkDevice device;
      VkRenderPass renderPass = nullptr;
      VkCommandPool commandPool = nullptr, ownCommandPool = nullptr;
      uint32_t graphicsQueueFamily = -1;
      std::vector<VkImageView> swapChainImageViews;
      std::vector<VkFramebuffer> framebuffers;
      std::vector<VkCommandBuffer> commandBuffers;

    private:
      static uint64_t frameId, completedFrameId;
      static inline uint64_t getNewFrameId() { return ++frameId; }
      static void completeFrame(uint64_t frameId);
    };
  }
}