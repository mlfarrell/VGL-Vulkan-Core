
#include "pch.h"
#include <stdexcept>
#include "VulkanFrameBuffer.h"
#include "VulkanInstance.h"
#include "VulkanAsyncResourceHandle.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    uint64_t VulkanFrameBuffer::frameId = 0, VulkanFrameBuffer::completedFrameId = 0;

    VulkanFrameBuffer::VulkanFrameBuffer(VulkanSwapChain *swapchain)
      : device(swapchain->getInstance()->getDefaultDevice())
    {
      auto &swapChainImages = swapchain->getImages();
      swapChainImageViews.resize(swapChainImages.size());

      for(int i = 0; i < (int)swapChainImages.size(); i++) 
      {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];

        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchain->getImageFormat();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if(vkCreateImageView(swapchain->getDevice(), &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) 
        {
          throw runtime_error("Failed to create Vulkan swapchain image views!");
        }
      }

      createRenderPass(swapchain);

      framebuffers.resize(swapChainImageViews.size());
      commandBuffers.resize(swapChainImageViews.size());
      for(int i = 0; i < (int)swapChainImages.size(); i++)
      {
        VkImageView attachments[] = {
          swapChainImageViews[i]
        };

        auto swapChainExtent = swapchain->getExtent();
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) 
          throw runtime_error("Failed to create Vulkan framebuffer!");        
      }

      graphicsQueueFamily = swapchain->getInstance()->getGraphicsQueueFamily();
    }

    VulkanFrameBuffer::~VulkanFrameBuffer()
    {
      if(renderPass)
        vkDestroyRenderPass(device, renderPass, nullptr);
      if(ownCommandPool)
        vkDestroyCommandPool(device, ownCommandPool, nullptr);

      for(auto framebuffer : framebuffers)
      {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      }
      for(auto imageView : swapChainImageViews) 
      {
        vkDestroyImageView(device, imageView, nullptr);
      }
    }

    VkCommandBuffer VulkanFrameBuffer::getCommandBuffer(int imageIndex)
    {
      if(!commandPool)
      {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if(vkCreateCommandPool(device, &poolInfo, nullptr, &ownCommandPool) != VK_SUCCESS)
          throw runtime_error("Unable to create Vulkan command pool");
        commandPool = ownCommandPool;
      }

      if(!commandBuffers[0])
      {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;       
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        if(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) 
          throw runtime_error("Unable to create Vulkan command buffers");
      }

      return commandBuffers[imageIndex];
    }

    void VulkanFrameBuffer::createRenderPass(VulkanSwapChain *swapchain)
    {
      VkAttachmentDescription colorAttachment = {};
      colorAttachment.format = swapchain->getImageFormat();
      colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

      colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

      VkAttachmentReference colorAttachmentRef = {};
      colorAttachmentRef.attachment = 0;
      colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

      VkSubpassDescription subpass = {};
      subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount = 1;
      subpass.pColorAttachments = &colorAttachmentRef;

      VkRenderPassCreateInfo renderPassInfo = {};
      renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      renderPassInfo.attachmentCount = 1;
      renderPassInfo.pAttachments = &colorAttachment;
      renderPassInfo.subpassCount = 1;
      renderPassInfo.pSubpasses = &subpass;

      VkSubpassDependency dependency = {};
      dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
      dependency.dstSubpass = 0;

      dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.srcAccessMask = 0;
      dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

      renderPassInfo.dependencyCount = 1;
      renderPassInfo.pDependencies = &dependency;

      if(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw runtime_error("Failed to create vulkan render pass!");
    }

    void VulkanFrameBuffer::completeFrame(uint64_t frameId)
    {
      if(frameId > completedFrameId)
      {
        completedFrameId = frameId;
        VulkanAsyncResourceMonitor::setCompletedFrame(completedFrameId);
      }
    }
  }
}
