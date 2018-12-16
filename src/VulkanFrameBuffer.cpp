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
#include <algorithm>
#include <stdexcept>
#include "VulkanInstance.h"
#include "VulkanFrameBuffer.h"
#include "VulkanAsyncResourceHandle.h"
#include "VulkanTexture.h"
#include "VulkanDescriptorPool.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VkImageView VulkanFrameBuffer::quickCreateImageView(VkImage image, VkFormat format, uint32_t baseLayer)
    {
      VkImageView imageView = VK_NULL_HANDLE;

      VkImageViewCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = image;

      createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      createInfo.format = format;
      createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

      createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = baseLayer;
      createInfo.subresourceRange.layerCount = 1;

      if(vkCreateImageView(device, &createInfo, nullptr, &imageView) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Failed to create Vulkan swapchain image views!");
      }

      return imageView;
    }

    VulkanFrameBuffer::VulkanFrameBuffer(VkDevice device, int numBuffers, const vector<ColorAttachment> &colorAttachments, 
      VulkanTexture *depthAttachment, bool clearLoadOp)
      : device(device), colorAttachments(colorAttachments), depthAttachment(depthAttachment), clearLoadOp(clearLoadOp)
    {
      instance = &VulkanInstance::currentInstance();
      if(device == VK_NULL_HANDLE)
        device = instance->getDefaultDevice();

      numColorAttachments = (int)colorAttachments.size();
      createRenderPass(nullptr);

      framebuffers.resize(numBuffers);
      commandBuffers.resize(numBuffers);
      fences.resize(numBuffers);

      for(int i = 0; i < numBuffers; i++)
      {
        VkImageView attachments[16];
        for(int j = 0; j < numColorAttachments; j++)
        {
          auto dims = colorAttachments[j].textures[i]->getDimensions();
          auto tex = colorAttachments[j].textures[i];

          if(colorAttachments[j].baseLayer == 0)
          {
            attachments[j] = tex->getImageView();
          }
          else
          {
            auto newImageView = quickCreateImageView(tex->get(), tex->getFormat(), colorAttachments[j].baseLayer);

            attachments[j] = newImageView;
            imageViews.push_back(newImageView);
          }

          if(w == 0 || h == 0)
          {
            w = dims.width;
            h = dims.height;
          }
          else if(w != dims.width || h != dims.height)
          {
            throw vgl_runtime_error("Vulkan Framebuffer constructed from color attachments of varying dimensions");
          }
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = numColorAttachments;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = w;
        framebufferInfo.height = h;
        framebufferInfo.layers = 1;

        if(depthAttachment)
        {
          attachments[numColorAttachments] = depthAttachment->getImageView();
          framebufferInfo.attachmentCount++;
        }

        if(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
          throw vgl_runtime_error("Failed to create Vulkan framebuffer!");
      }

      //for now, we only create these for non-swapchain framebuffers
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

      for(int i = 0; i < numBuffers; i++)
      {
        vkCreateFence(device, &fenceInfo, nullptr, &fences[i]);
      }

      //not utilized (I use the main frame ones in this case)
      for(int i = 0; i < numBuffers; i++)
      {
        descriptorPools.push_back(new VulkanDescriptorPool(device, 64, 32, 0, 32, 0));
      }

      graphicsQueueFamily = instance->getGraphicsQueueFamily();
      isSwapChain = false;
    }

    VulkanFrameBuffer::VulkanFrameBuffer(VulkanSwapChain *swapchain, bool clearLoadOp)
      : device(swapchain->getInstance()->getDefaultDevice()), clearLoadOp(clearLoadOp)
    {
      auto &swapChainImages = swapchain->getImages();
      imageViews.resize(swapChainImages.size());

      for(int i = 0; i < (int)swapChainImages.size(); i++)
      {
        imageViews[i] = quickCreateImageView(swapChainImages[i], swapchain->getImageFormat());
      }

      if(swapchain->getNumMultiSamples() > 1)
      {
        imageViews.push_back(quickCreateImageView(swapchain->getMSAAColorTarget()->get(), swapchain->getMSAAColorTarget()->getFormat()));
      }

      createRenderPass(swapchain);

      framebuffers.resize(imageViews.size());
      commandBuffers.resize(imageViews.size());
      for(int i = 0; i < (int)swapChainImages.size(); i++)
      {
        VkImageView attachments[3] = {
          imageViews[i]
        };

        auto swapChainExtent = swapchain->getExtent();
        w = swapChainExtent.width;
        h = swapChainExtent.height;

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = w;
        framebufferInfo.height = h;
        framebufferInfo.layers = 1;

        if(auto depthAttachmentTex = swapchain->getDepthAttachment())
        {
          attachments[1] = depthAttachmentTex->getImageView();
          framebufferInfo.attachmentCount = 2;
        }

        if(swapchain->getNumMultiSamples() > 1)
        {
          attachments[0] = imageViews[swapChainImages.size()];
          attachments[1] = swapchain->getMSAADepthTarget()->getImageView();
          attachments[2] = imageViews[i];
          framebufferInfo.attachmentCount++;
        }

        if(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
          throw vgl_runtime_error("Failed to create Vulkan framebuffer!");
      }

      //these can dynamically grow if more space is needed
      for(int i = 0; i < (int)swapChainImages.size(); i++)
      {
        descriptorPools.push_back(new VulkanDescriptorPool(device, 512, 256, 0, 256, 0));
      }

      graphicsQueueFamily = swapchain->getInstance()->getGraphicsQueueFamily();
      isSwapChain = true;
    }

    VulkanFrameBuffer::~VulkanFrameBuffer()
    {
      if(!isSwapChain && !fences.empty() && commandBuffers[0] != VK_NULL_HANDLE)
      {
        //For the offscreen Framebuffer case, there's alot here which can be deleted while still in use..
        //I'm trying out justing wait on a fence instead of using the async resource handle system for all of them
        vkWaitForFences(device, (uint32_t)fences.size(), fences.data(), VK_TRUE, numeric_limits<uint64_t>::max());
      }

      if(renderPass)
        vkDestroyRenderPass(device, renderPass, nullptr);
      if(ownCommandPool)
        vkDestroyCommandPool(device, ownCommandPool, nullptr);

      for(auto framebuffer : framebuffers)
      {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      }
      for(auto imageView : imageViews) 
      {
        vkDestroyImageView(device, imageView, nullptr);
      }
      for(auto fence : fences)
      {
        vkDestroyFence(device, fence, nullptr);
      }
      for(auto pool : descriptorPools)
      {
        delete pool;
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
          throw vgl_runtime_error("Unable to create Vulkan command pool");
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
          throw vgl_runtime_error("Unable to create Vulkan command buffers");
      }

      //the swapchain has it's own fences that are waited on ahead of this call
      if(!isSwapChain)
      {
        vkWaitForFences(device, 1, &fences[imageIndex], VK_TRUE, numeric_limits<uint64_t>::max());
        vkResetFences(device, 1, &fences[imageIndex]);
      }

      if(descriptorPools[imageIndex])
        vkResetDescriptorPool(device, descriptorPools[imageIndex]->get(), 0);

      return commandBuffers[imageIndex];
    }

    VulkanDescriptorPool *VulkanFrameBuffer::getCurrentDescriptorPool(int imageIndex)
    {
      return descriptorPools[imageIndex];
    }

    void VulkanFrameBuffer::replaceDescriptorPool(int imageIndex, VulkanDescriptorPool *pool)
    {
      delete descriptorPools[imageIndex];
      descriptorPools[imageIndex] = pool;
    }

    void VulkanFrameBuffer::createRenderPass(VulkanSwapChain *swapchain)
    {      
      bool depth = false;
      bool shaderResource = false;

      VkAttachmentDescription attachments[17] = { 0 };
      VkAttachmentReference colorAttachmentRefs[16] = {};
      int attachmentPos = 0;

      //we'll keep things simple by keeping MSAA resolve sources and resolve targets contiguous and separate
      int firstColorResolveTargetIndex = -1;

      if(swapchain)
        numColorAttachments = 1;

      for(int i = 0; i < numColorAttachments; i++,attachmentPos++)
      {
        VkAttachmentDescription &colorAttachment = attachments[i];

        colorAttachment.format = swapchain ? swapchain->getImageFormat() : colorAttachments[i].textures[0]->getFormat();
        colorAttachment.samples = swapchain ? swapchain->getNumMultiSamples() : colorAttachments[i].textures[0]->getMultiSamples();
        colorAttachment.loadOp = (clearLoadOp) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        if(swapchain)
        {
          if(swapchain->getNumMultiSamples() <= 1)
          {
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
          }
          else
          {
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          }
        }
        else
        {
          colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

          if(colorAttachments[i].textures[0]->isShaderResource())
          {
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shaderResource = true;
          }
        }

        //be sure to discard likely-unused multisample buffers
        if((int)colorAttachment.samples > 1)
          colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        colorAttachmentRefs[i].attachment = i;
        colorAttachmentRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if(!swapchain && colorAttachments[i].resolveToTarget != DONT_RESOLVE)
        {
          if(firstColorResolveTargetIndex >= 0)
            firstColorResolveTargetIndex = min((int8_t)firstColorResolveTargetIndex, (int8_t)colorAttachments[i].resolveToTarget);
          else
            firstColorResolveTargetIndex = (int8_t)colorAttachments[i].resolveToTarget;
        }
        else if(firstColorResolveTargetIndex >= 0 && i < firstColorResolveTargetIndex)
        {
          throw vgl_runtime_error("You currently cannot interleave MSAA target and resolve targets when using VulkanFrameBuffer");
        }
      }

      VkAttachmentReference depthAttachmentRef = {};

      VkSubpassDescription subpass = {};
      subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount = (firstColorResolveTargetIndex == -1) ? numColorAttachments : firstColorResolveTargetIndex;
      subpass.pColorAttachments = colorAttachmentRefs;

      if(swapchain && swapchain->getNumMultiSamples() > 1)
      {
        if(!swapchain->getDepthAttachment())
          throw vgl_runtime_error("Vulkan Swapchain with MSAA requires depth attachment to be also enabled");

        colorAttachmentRefs[1].attachment = 2;
        colorAttachmentRefs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        subpass.pResolveAttachments = &colorAttachmentRefs[1];
      }
      else
      {
        subpass.pResolveAttachments = (firstColorResolveTargetIndex == -1) ? nullptr : colorAttachmentRefs+firstColorResolveTargetIndex;
      }

      VulkanTexture *depthAttachmentTex = depthAttachment;
      if(swapchain)
      {
        depthAttachmentTex = (swapchain->getNumMultiSamples() <= 1) ? swapchain->getDepthAttachment() : swapchain->getMSAADepthTarget();
      }

      if(depthAttachmentTex)
      {
        VkAttachmentDescription &depthAttachment = attachments[attachmentPos];

        depthAttachment.format = depthAttachmentTex->getFormat();
        depthAttachment.samples = swapchain ? swapchain->getNumMultiSamples() : depthAttachmentTex->getMultiSamples();
        depthAttachment.loadOp = (clearLoadOp) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthAttachmentRef.attachment = attachmentPos;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        depth = true;

        attachmentPos++;
      }

      if(swapchain && swapchain->getNumMultiSamples() > 1)
      {
        //sigh... this is ugly as shit.. need to append actual swapchain image to end when MSAA is enabled
        VkAttachmentDescription &colorAttachment = attachments[attachmentPos];

        colorAttachment.format = swapchain->getImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachmentPos++;
      }

      VkRenderPassCreateInfo renderPassInfo = {};
      renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      renderPassInfo.attachmentCount = attachmentPos;
      renderPassInfo.pAttachments = attachments;
      renderPassInfo.subpassCount = 1;
      renderPassInfo.pSubpasses = &subpass;

      VkSubpassDependency dependency = {};
      dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
      dependency.dstSubpass = 0;

      if(!shaderResource)
      {
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      }
      else
      {
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

        if(shaderResource)
        {
          dependency.srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
          dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
          dependency.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
          dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        }
      }

      renderPassInfo.dependencyCount = 1;
      renderPassInfo.pDependencies = &dependency;

      if(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create vulkan render pass!");
    }
  }
}
