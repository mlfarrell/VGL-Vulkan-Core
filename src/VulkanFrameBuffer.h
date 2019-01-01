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
#include "SequentialIdentifier.h"

namespace vgl
{
  namespace core
  {
    class VulkanInstance;
    class VulkanDescriptorPool;
    class VulkanTexture;

    class VulkanFrameBuffer : public SequentialIdentifier
    {
    public:
      friend class VulkanSwapChainBase;

      ///Constructs a high-level framebuffer (a collection of framebuffers & command buffers; one for each image in the swapchain)
      VulkanFrameBuffer(VulkanSwapChain *swapchain, bool clearLoadOp=true);

      enum ResolveTarget : int8_t { DONT_RESOLVE = -1 };
      struct ColorAttachment
      {
        //Array of N texture pointers where N is the number of buffers needed
        VulkanTexture **textures;
        int baseLayer;
        ResolveTarget resolveToTarget;
      };

      ///When constructing framebuffers that will be used in lockstep with swapchain images numBuffers should be 
      /// set to the number of images in the swapchain and will define the domain for the imageIndex arguments
      /// throughout this class.  For simple one-shot render-to-texture, numBuffers should be 1
      VulkanFrameBuffer(VkDevice device, int numBuffers, const std::vector<ColorAttachment> &colorAttachments, VulkanTexture *depthAttachment=nullptr,
        bool clearLoadOp=true);

      ~VulkanFrameBuffer();

      VkCommandBuffer getCommandBuffer(int imageIndex=0);

      inline VkFramebuffer get(int imageIndex=0) { return framebuffers[imageIndex]; }
      inline VkRenderPass getRenderPass() { return renderPass; }
      inline VkFence getFence(int imageIndex=0) { return fences[imageIndex]; }

      inline VkExtent2D getSize() { return { w, h }; }

      VulkanDescriptorPool *getCurrentDescriptorPool(int imageIndex=0);
      void replaceDescriptorPool(int imageIndex, VulkanDescriptorPool *pool);
      
      void setClearColorValue(VkClearColorValue color);
      inline VkClearColorValue getClearColorValue() { return clearColorValue; }

    protected:
      void createRenderPass(VulkanSwapChain *swapchain);
      VkImageView quickCreateImageView(VkImage image, VkFormat format, uint32_t baseLayer=0);

      VulkanInstance *instance;
      VkDevice device;
      VkRenderPass renderPass = nullptr;
      VkCommandPool commandPool = nullptr, ownCommandPool = nullptr;
      uint32_t graphicsQueueFamily = -1;
      std::vector<VkImageView> imageViews; //only used if FBO can't grab it from attached texture
      std::vector<VkFramebuffer> framebuffers;
      std::vector<VkCommandBuffer> commandBuffers;
      bool isSwapChain = false;

      //note that when constructed with swapchain, the swapchain fences will be used in place of these
      std::vector<VkFence> fences;

      //these are not defined if the framebuffer was created with a swapchain
      std::vector<ColorAttachment> colorAttachments;
      VulkanTexture *depthAttachment = nullptr;
      int numColorAttachments = 0;
      bool clearLoadOp = true;
      VkClearColorValue clearColorValue = { 0.0f, 0.0f, 0.0f, 1.0f };
      uint32_t w = 0, h = 0;

      //the descriptor pool thing is nowhere close to finalized yet..
      //still figuring out how I want this to work..
      std::vector<VulkanDescriptorPool *> descriptorPools;
    };

    typedef VulkanFrameBuffer FrameBuffer;
  }
}
