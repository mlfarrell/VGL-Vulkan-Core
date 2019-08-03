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
#include <iostream>
#include <set>
#include <algorithm>
#include "VulkanInstance.h"
#include "VulkanTexture.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"
#include "VulkanFrameBuffer.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "StateMachine.h"
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanTexture::VulkanTexture(TextureType type) 
      : type(type)
    {
      instance = &VulkanInstance::currentInstance();
      device = instance->getDefaultDevice();
      queue = instance->getGraphicsQueue();
      commandPool = instance->getTransferCommandPool();
      memset(&samplerState, 0, sizeof(samplerState));
    }

    VulkanTexture::VulkanTexture(VulkanSwapChain *swapchain, int imageIndex)
    {
      instance = &VulkanInstance::currentInstance();
      device = instance->getDefaultDevice();
      queue = instance->getGraphicsQueue();
      commandPool = instance->getTransferCommandPool();

      //intentionally not initializing the handles here since the swapchain images/memory is externally managed
      image = swapchain->getImages()[imageIndex];
      width = swapchain->getExtent().width;
      height = swapchain->getExtent().height;
      depth = 1;
      numArrayLayers = 1;
      numMipLevels = 1;
      format = swapchain->getImageFormat();
      isSwapchainImage = true;
    }

    VulkanTexture::VulkanTexture(VkDevice device, TextureType type, VkCommandPool commandPool, VkQueue queue)
      : device(device), commandPool(commandPool), queue(queue), type(type)
    {
      instance = &VulkanInstance::currentInstance();
      if(device == VK_NULL_HANDLE)
        device = instance->getDefaultDevice();
      memset(&samplerState, 0, sizeof(samplerState));
    }

    VulkanTexture::~VulkanTexture()
    {
      auto &mm = *instance->getMemoryManager();

      //trying out a new policy of always keeping outgoing-handles around until next frame completes
      if(imageHandle)
      {
        uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();
        retainResourcesUntilFrameCompletion(frameId);
      }

      if(imageHandle && imageHandle->release())
        delete imageHandle;
      if(stagingBufferHandle && stagingBufferHandle->release())
        delete stagingBufferHandle;
      if(samplerHandle && samplerHandle->release())
        delete samplerHandle;
      safeUnbind();
    }

    VkImage VulkanTexture::get()
    {
      return image;
    }

    void VulkanTexture::putDescriptor(VkDescriptorSet set, uint32_t binding, uint32_t arrayElement)
    {
      VkWriteDescriptorSet write = {};
      VkDescriptorImageInfo imageInfo = {};

      putDescriptor(write, imageInfo, set, binding, arrayElement);
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    
    void VulkanTexture::putDescriptor(VkWriteDescriptorSet &write, VkDescriptorImageInfo &imageInfo, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement)
    {
      if(deferImageCreation && !imageHandle)
      {
        createImage();
        for(int i = 0; i < numArrayLayers; i++)
          copyToImage(i, 0, instance->getTransferCommandBuffer().first);
        
        createImageView();
        createSampler();
        
        imageHandle = VulkanAsyncResourceHandle::newImage(instance->getResourceMonitor(), device, image, imageView, imageAllocation);
      }
      
      //sampler out of date?
      if(samplerDirty)
      {
        createSampler();
      }
      
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = imageView;
      imageInfo.sampler = sampler;
      
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = set;
      write.dstBinding = binding;
      write.dstArrayElement = arrayElement;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &imageInfo;
      write.pNext = nullptr;
      write.pBufferInfo = nullptr;
      write.pTexelBufferView = nullptr;
    }

    /*void VulkanTexture::copyDescriptor(VkDescriptorSet srcSet, VkDescriptorSet dstSet, uint32_t srcBinding, uint32_t dstBinding)
    {
      VkCopyDescriptorSet copy = {};

      copy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
      copy.srcSet = srcSet;
      copy.dstSet = dstSet;
      copy.srcBinding = srcBinding;
      copy.dstBinding = dstBinding;
      copy.descriptorCount = 1;
      vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
    }*/

    static inline VkSampleCountFlagBits samplesToSampleCountBits(uint32_t numSamples)
    {
      if(numSamples > 64)
        numSamples = 64;
      if(numSamples == 0 || numSamples & (numSamples-1))
        numSamples = 1;

      return (VkSampleCountFlagBits)numSamples;
    }

    void VulkanTexture::imageData(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, const void *data, size_t numBytes, uint32_t layerIndex, uint32_t level, uint32_t numSamples, VkCommandBuffer transferCommandBuffer)
    {
      bool shouldReinit = false, newImage = false;

      if(stagingBuffer)
      {
        if(type == TT_CUBE_MAP)
          shouldReinit = (this->width != width || this->height != height || this->format != format || (mipmapEnabled && numMipLevels == 1) || (!mipmapEnabled && numMipLevels != 1));
        else
          shouldReinit = true;
      }

      if(shouldReinit)
      {
        //trying out a new policy of always keeping outgoing-handles around until next frame completes
        if(imageHandle)
        {
          uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();
          retainResourcesUntilFrameCompletion(frameId);
        }

        if(stagingBufferHandle->release())
          delete stagingBufferHandle;
        stagingBufferHandle = nullptr;
        stagingBuffer = VK_NULL_HANDLE;

        if(imageHandle->release())
          delete imageHandle;
        imageHandle = nullptr;
        safeUnbind();
      }

      if(type != TT_CUBE_MAP)
        numArrayLayers = 1;
      else
        numArrayLayers = 6;
      size = numBytes*numArrayLayers;
      if(size == 0)
        return;

      VkBufferCreateInfo bufferInfo = {};
      VulkanMemoryManager::Suballocation alloc;

      if(!stagingBuffer)
      {
        if(stagingBufferHandle && stagingBufferHandle->release())
          delete stagingBufferHandle;
        createStagingBuffer(false);
      }

      if(!imageHandle)
      {
        newImage = true;
        this->width = width;
        this->height = height;
        this->depth = depth;
        this->format = format;
        this->numMultiSamples = (int)samplesToSampleCountBits(numSamples);
        if(mipmapEnabled)
          numMipLevels = (uint32_t)(log2(max(width, height))) + 1;
        else
          numMipLevels = 1;

        if(!deferImageCreation)
          createImage();
      }

      alloc = stagingBufferAllocation;
      void *mappedPtr;
      if(vkMapMemory(device, alloc.memory, alloc.offset + numBytes*layerIndex, numBytes, 0, &mappedPtr) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
      }
      memcpy(mappedPtr, data, numBytes);
      vkUnmapMemory(device, stagingBufferAllocation.memory);

      isShaderRsrc = true;

      if(!deferImageCreation)
      {
        if(newImage)
        {
          createImageView();
          createSampler();

          imageHandle = VulkanAsyncResourceHandle::newImage(instance->getResourceMonitor(), device, image, imageView, imageAllocation);
        }

        copyToImage(layerIndex, level, transferCommandBuffer);
      }
    }

    void VulkanTexture::initImage(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, uint32_t usage, uint32_t layerIndex, uint32_t numSamples, VkCommandBuffer transferCommandBuffer)
    {
      bool shouldReinit = false;

      if(type == TT_CUBE_MAP)
        shouldReinit = (this->width != width || this->height != height || this->format != format);
      else
        shouldReinit = true;

      if(shouldReinit)
      {
        if(stagingBufferHandle && stagingBufferHandle->release())
        {
          delete stagingBufferHandle;
          stagingBufferHandle = nullptr;
        }
        stagingBuffer = VK_NULL_HANDLE;

        if(imageHandle)
        {
          //trying out a new policy of always keeping outgoing-handles around until next frame completes
          uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();
          retainResourcesUntilFrameCompletion(frameId);

          if(imageHandle->release())
          {
            delete imageHandle;
            imageHandle = nullptr;
          }
          safeUnbind();
        }
      }
      else if(imageHandle)
      {
        return;
      }

      if(type != TT_CUBE_MAP)
        numArrayLayers = 1;
      else
        numArrayLayers = 6;
      if(width == 0 || height == 0 || depth == 0)
        return;

      this->width = width;
      this->height = height;
      this->depth = depth;
      this->format = format;
      this->numMultiSamples = (int)samplesToSampleCountBits(numSamples);
      numMipLevels = 1;

      //TODO: defer this as well if deferImageCreation is true
      VkImageCreateInfo imageInfo = {};
      imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      switch(type)
      {
        case TT_1D:
          imageInfo.imageType = VK_IMAGE_TYPE_1D;
        break;
        case TT_2D:
          imageInfo.imageType = VK_IMAGE_TYPE_2D;
        break;
        case TT_CUBE_MAP:
          numArrayLayers = 6;
          imageInfo.imageType = VK_IMAGE_TYPE_2D;
        break;
      }
      imageInfo.extent.width = width;
      imageInfo.extent.height = height;
      imageInfo.extent.depth = depth;
      imageInfo.format = format;
      imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = numArrayLayers;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      if(type == TT_CUBE_MAP)
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

      if(usage & U_DEPTH_STENCIL_ATTACHMENT)
      {
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        isDepth = true;
      }
      if(usage & U_COLOR_ATTACHMENT)
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      if(usage & U_SAMPLED_IMAGE)
        imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      if(readbackEnabled)
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.samples = samplesToSampleCountBits(numSamples);
      imageInfo.mipLevels = 1;

      if(numSamples > 1)
      {
        //I can cache these if it becomes an issue (this routine with msaa is rarely called)
        auto physicalDevice = instance->getPhysicalDevice();
        VkImageFormatProperties formatProps;

        if(vkGetPhysicalDeviceImageFormatProperties(physicalDevice, format, imageInfo.imageType, imageInfo.tiling, imageInfo.usage, imageInfo.flags, &formatProps) == VK_SUCCESS)
        {
          while(numSamples > 1 && (formatProps.sampleCounts & imageInfo.samples) == 0)
          {
            numSamples /= 2;
            imageInfo.samples = samplesToSampleCountBits(numSamples);
          }
        }

        this->numMultiSamples = (int)samplesToSampleCountBits(numSamples);
      }

      if(vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan image!");

      VkMemoryRequirements memRequirements;
      VulkanMemoryManager::Suballocation alloc;

      vkGetImageMemoryRequirements(device, image, &memRequirements);

      alloc = instance->getMemoryManager()->allocate(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        memRequirements.size, memRequirements.alignment, true);
      if(!alloc)
      {
        //must be out of GPU memory, fallback on whater we can use
        alloc = instance->getMemoryManager()->allocate(memRequirements.memoryTypeBits, 0,
          memRequirements.size, memRequirements.alignment, true);
        isResident = false;
      }
      else
      {
        isResident = true;
      }
      imageAllocation = alloc;

      vkBindImageMemory(device, image, alloc.memory, alloc.offset);

      VkImageLayout layout = (!isDepth) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

      if(usage & U_SAMPLED_IMAGE)
      {
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        isShaderRsrc = true;
      }

      auto commandBuffer = transferCommandBuffer;
      if(!transferCommandBuffer)
        commandBuffer = startOneTimeCommandBuffer();
      transitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, layout, commandBuffer);
      if(!transferCommandBuffer)
        submitOneTimeCommandBuffer(commandBuffer);

      createImageView();
      if(isShaderRsrc)
        createSampler();
      imageHandle = VulkanAsyncResourceHandle::newImage(instance->getResourceMonitor(), device, image, imageView, imageAllocation);
    }

    void VulkanTexture::readImageData(uint32_t x, uint32_t y, uint32_t readWidth, uint32_t readHeight, uint32_t layer, uint32_t level, void *data)
    {
      //TODO: use blitting in here to speed this up!

      if(!stagingBufferHandle)
      {
        //TODO: make this "4" determined by image format
        size = width*height*4;
        createStagingBuffer(true);
      }

      VkDeviceSize linearCopySize = 0;
      switch(format)
      {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
          linearCopySize = readWidth*readHeight*4;
        break;
        default:
          throw vgl_runtime_error("Unsupported image format for VulkanTexture::readImageData()");
        break;
      }

      copyFromImage(x, y, readWidth, readHeight, layer, level, VK_NULL_HANDLE, true);

      void *mappedPtr;
      if(vkMapMemory(device, stagingBufferAllocation.memory, stagingBufferAllocation.offset + size*layer, linearCopySize, 0, &mappedPtr) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
      }

      if(format == VK_FORMAT_B8G8R8A8_UNORM)
      {
        uint32_t *srcPixel = (uint32_t *)mappedPtr;
        uint32_t *dstPixel = (uint32_t *)data;

        for(int i = 0; i < linearCopySize/4; i++)
        {
          uint32_t val = *srcPixel;
          
          //BGRA -> RGBA
          val = ((((val) & 0xff000000)) |
            (((val) & 0x00ff0000) >> 16) |
            (((val) & 0x0000ff00)) |
            (((val) & 0x000000ff) << 16));
          *dstPixel = val;

          srcPixel++;
          dstPixel++;
        }
      }
      else
      {
        memcpy(data, mappedPtr, linearCopySize);
      }

      vkUnmapMemory(device, stagingBufferAllocation.memory);
    }

    void VulkanTexture::setFilters(SamplerFilterType min, SamplerFilterType mag)
    {
      minFilter = min;
      magFilter = mag;
      samplerDirty = true;
      if(mipmapEnabled)
        setMipmap(true, samplerState.mipLodBias);
      else
        setMipmap(false, samplerState.mipLodBias);
    }

    void VulkanTexture::setAnisotropicFiltering(bool enabled, int maxSamples)
    {
      samplerState.anisotropyEnable = enabled ? VK_TRUE : VK_FALSE;
      samplerState.maxAnisotropy = (float)maxSamples;
      samplerDirty = true;
    }

    void VulkanTexture::setWrapMode(WrapMode mode)
    {
      switch(mode)
      {
        case WM_REPEAT:
          samplerState.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
          samplerState.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
          //samplerState.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
        case WM_CLAMP:
          samplerState.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
          samplerState.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
          //samplerState.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
        case WM_MIRRORED_REPEAT:
          samplerState.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
          samplerState.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
          //samplerState.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
      }

      samplerDirty = true;
    }

    void VulkanTexture::setMipmap(bool enabled, float lodBias)
    {
      mipmapEnabled = enabled;

      if(enabled)
      {
        samplerState.mipmapMode = (minFilter == ST_LINEAR_MIPMAP_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerState.mipLodBias = lodBias;
        samplerState.minLod = 0;
        samplerState.maxLod = (float)numMipLevels;
      }
    }

    void VulkanTexture::generateMipmaps(uint32_t layerIndex, VkCommandBuffer transferCommandBuffer)
    {
      static bool checkedFilterSupport = false;

      if(!checkedFilterSupport)
      {
        auto physicalDevice = instance->getPhysicalDevice();

        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) 
          throw vgl_runtime_error("Texture image format does not support linear blitting!");        

        checkedFilterSupport = true;
      }

      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.image = image;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseArrayLayer = layerIndex;
      barrier.subresourceRange.layerCount = 1;
      barrier.subresourceRange.levelCount = 1;

      int32_t mipWidth = width;
      int32_t mipHeight = height;

      for(uint32_t i = 1; i < numMipLevels; i++) 
      {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        auto srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage = srcStage;
        vkCmdPipelineBarrier(transferCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit = {};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = layerIndex;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { (mipWidth > 1) ? mipWidth / 2 : 1, (mipHeight > 1) ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = layerIndex;
        blit.dstSubresource.layerCount = 1;

        auto srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        auto dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdBlitImage(transferCommandBuffer, image, srcLayout, image, dstLayout, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        vkCmdPipelineBarrier(transferCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        if(mipWidth > 1) 
          mipWidth /= 2;
        if(mipHeight > 1) 
          mipHeight /= 2;
      }

      auto srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      barrier.subresourceRange.baseMipLevel = numMipLevels - 1;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(transferCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void VulkanTexture::setReadbackEnabled(bool enabled)
    {
      readbackEnabled = enabled;
    }

    void VulkanTexture::transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerIndex, VkCommandBuffer transferCommandBuffer)
    {
      auto hasStencilComponent = [](VkFormat format) {
        return (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT);
      };

      VkImageMemoryBarrier barrier = {};
      VkPipelineStageFlags sourceStage;
      VkPipelineStageFlags destinationStage;

      //this is getting out of hand...
      if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
      {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        //destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; //we may want to read these textures from vertex shaders
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      }

      //these two haven't been tested yet
      else if(oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      }

      else if(oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
      {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }

      else if(oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }

      else if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        //destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; //we may want to read these textures from vertex shaders
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = /*VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |*/ VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        //destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; //we may want to read these textures from vertex shaders
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      }
      else if(oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      }
      else
      {
        throw vgl_runtime_error("Unsupported layout transition!");
      }

      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = oldLayout;
      barrier.newLayout = newLayout;

      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

      barrier.image = image;
      if(newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        isDepth = true;

        if(hasStencilComponent(format))
        {
          barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
          isStencil = true;
        }
      }
      else
      {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      }
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = numMipLevels;
      if(layerIndex == 0xFFFFFFFF)
      {
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = numArrayLayers;
      }
      else
      {
        barrier.subresourceRange.baseArrayLayer = layerIndex;
        barrier.subresourceRange.layerCount = 1;
      }

      vkCmdPipelineBarrier(transferCommandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void VulkanTexture::transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer transferCommandBuffer)
    {
      transitionLayout(oldLayout, newLayout, 0xFFFFFFFF, transferCommandBuffer);
    }

    VkCommandBuffer VulkanTexture::startOneTimeCommandBuffer()
    {
      VkCommandBufferAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocInfo.commandPool = commandPool;
      allocInfo.commandBufferCount = 1;

      VkCommandBuffer commandBuffer;
      vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

      VkCommandBufferBeginInfo beginInfo = {};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

      vkBeginCommandBuffer(commandBuffer, &beginInfo);
      return commandBuffer;
    }

    void VulkanTexture::submitOneTimeCommandBuffer(VkCommandBuffer commandBuffer, bool wait)
    {
      VkFence transferFence;
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

      if(vkCreateFence(device, &fenceInfo, nullptr, &transferFence) != VK_SUCCESS)
        throw vgl_runtime_error("Could not create Vulkan Fence!");

      vkEndCommandBuffer(commandBuffer);

      VkSubmitInfo submitInfo = {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &commandBuffer;

      vkQueueSubmit(queue, 1, &submitInfo, transferFence);

      //just a note, fences must alwasy be the LAST entry in these collections
      if(!wait)
      {
        auto resourceMonitor = instance->getResourceMonitor();
        auto fenceHandle = VulkanAsyncResourceHandle::newFence(resourceMonitor, device, transferFence);
        auto cmdBufHandle = VulkanAsyncResourceHandle::newCommandBuffer(resourceMonitor, device, commandBuffer, commandPool);
        VulkanAsyncResourceCollection transferResources(resourceMonitor, fenceHandle, {
          stagingBufferHandle, imageHandle,
          cmdBufHandle, fenceHandle
          });
        resourceMonitor->append(move(transferResources));
        fenceHandle->release();
        cmdBufHandle->release();
      }
      else
      {
        vkWaitForFences(device, 1, &transferFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
        vkDestroyFence(device, transferFence, nullptr);
      }
    }

    void VulkanTexture::createStagingBuffer(bool needsTransferDest)
    {
      VkBufferCreateInfo bufferInfo = {};
      VkMemoryRequirements memRequirements;
      VulkanMemoryManager::Suballocation alloc;

      bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferInfo.size = size;
      bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

      if(needsTransferDest)
      {
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        stagingBufferTransferDst = true;
      }

      if(vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Failed to create vertex buffer!");
      }

      vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

      alloc = instance->getMemoryManager()->allocate(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        memRequirements.size, memRequirements.alignment);
      stagingBufferAllocation = alloc;

      vkBindBufferMemory(device, stagingBuffer, alloc.memory, alloc.offset);
      stagingBufferHandle = VulkanAsyncResourceHandle::newBuffer(instance->getResourceMonitor(), device, stagingBuffer, alloc);
    }
    
    static bool isCompressedTextureFormat(VkFormat format)
    {
      switch(format)
      {
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
          return true;
        default:
          return false;
      }
      
      return false;
    }

    void VulkanTexture::createImage()
    {
      VkMemoryRequirements memRequirements;
      VkImageCreateInfo imageInfo = {};

      imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      switch(type)
      {
        case TT_1D:
          imageInfo.imageType = VK_IMAGE_TYPE_1D;
        break;
        case TT_2D:
          imageInfo.imageType = VK_IMAGE_TYPE_2D;
        break;
        case TT_CUBE_MAP:
          numArrayLayers = 6;
          imageInfo.imageType = VK_IMAGE_TYPE_2D;
        break;
      }
      imageInfo.extent.width = width;
      imageInfo.extent.height = height;
      imageInfo.extent.depth = depth;
      imageInfo.format = format;
      imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.arrayLayers = numArrayLayers;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.samples = (VkSampleCountFlagBits)numMultiSamples;
      imageInfo.mipLevels = numMipLevels;
      if(type == TT_CUBE_MAP)
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

      if((mipmapEnabled && !isCompressedTextureFormat(format) && autoGenerateMipmaps) || readbackEnabled)
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

      if(vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan image!");

      vkGetImageMemoryRequirements(device, image, &memRequirements);
      auto alloc = instance->getMemoryManager()->allocate(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        memRequirements.size, memRequirements.alignment, true);

      if(!alloc)
      {
        //must be out of GPU memory, fallback on whatever we can use
        alloc = instance->getMemoryManager()->allocate(memRequirements.memoryTypeBits, 0,
          memRequirements.size, memRequirements.alignment, true);
        isResident = false;
      }
      else
      {
        isResident = true;
      }

      imageAllocation = alloc;
      vkBindImageMemory(device, image, alloc.memory, alloc.offset);
    }

    void VulkanTexture::createImageView()
    {
      VkImageViewCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = image;
      createInfo.format = format;

      switch(type)
      {
        case TT_1D:
          createInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
        break;
        case TT_2D:
          createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;
        case TT_CUBE_MAP:
          createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        break;
      }

      if(!isDepth && !isStencil)
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
      if(isDepth)
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if(isStencil)
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = numMipLevels;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = numArrayLayers;

      if(vkCreateImageView(device, &createInfo, nullptr, &imageView) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Failed to create Vulkan texture image view!");
      }
    }

    void VulkanTexture::createSampler()
    {
      if(!samplerHandle || samplerDirty)
      {
        if(samplerHandle)
        {
          //async-releasing old sampler handle to be safe
          auto resourceMonitor = instance->getResourceMonitor();
          auto handles = vector<VulkanAsyncResourceHandle *> { samplerHandle };
          uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();

          VulkanAsyncResourceCollection frameResources(resourceMonitor, frameId, handles);
          resourceMonitor->append(move(frameResources));

          samplerHandle->release();
        }

        samplerState.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerState.magFilter = (minFilter == ST_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        samplerState.minFilter = (magFilter == ST_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        samplerState.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerState.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerState.minLod = 0;
        if(mipmapEnabled)
          samplerState.maxLod = (float)numMipLevels;

        if(vkCreateSampler(device, &samplerState, nullptr, &sampler) != VK_SUCCESS) 
          throw std::runtime_error("Failed to create Vulkan texture sampler!");

        samplerHandle = VulkanAsyncResourceHandle::newSampler(instance->getResourceMonitor(), device, sampler);
        samplerDirty = false;
      }
    }

    void VulkanTexture::copyToImage(uint32_t layerIndex, uint32_t level, VkCommandBuffer transferCommandBuffer)
    {
      auto copyCommandBuffer = transferCommandBuffer;

      if(!size || !stagingBuffer)
        verr << "Vulkan Warning:  Attempting to call VulkanTexture::copyToImage() with invalid staging buffer!" << endl;

      if(!transferCommandBuffer)
      {
        copyCommandBuffer = startOneTimeCommandBuffer();
      }

      VkBufferImageCopy region = {};
      region.bufferOffset = (size / numArrayLayers) * layerIndex;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;

      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = level;
      region.imageSubresource.baseArrayLayer = layerIndex;
      region.imageSubresource.layerCount = 1;

      region.imageOffset = { 0, 0, 0 };
      region.imageExtent = { width, height, depth };

      transitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerIndex, copyCommandBuffer);
      vkCmdCopyBufferToImage(copyCommandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      if(!mipmapEnabled || !autoGenerateMipmaps || isCompressedTextureFormat(format))
      {
        transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerIndex, copyCommandBuffer);
      }
      else
      {
        //this is insanely inefficent for the layerCount > 1 (cubemap) case
        generateMipmaps(layerIndex, copyCommandBuffer);
      }

      if(!transferCommandBuffer)
      {
        submitOneTimeCommandBuffer(copyCommandBuffer);
      }
      else if(instance->getCurrentTransferCommandBuffer().first)
      {
        bool frame = false;

#ifndef VGL_VULKAN_CORE_STANDALONE
        auto csm = static_cast<vgl::CoreStateMachine *>(instance->getParentRenderer());

        if(csm->isInsideFrame())
          frame = true;
#endif

        if(frame)
        {
          uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();
          auto resourceMonitor = instance->getResourceMonitor();

          VulkanAsyncResourceCollection frameResources(resourceMonitor, frameId, {
            stagingBufferHandle, imageHandle,
          });
          resourceMonitor->append(move(frameResources));
        }
        else
        {
          //by passing a null fence, we mark these resources as committed to a command buffer that hasn't
          //been submitted yet, and the fence will be provided when the transfer buffer is finally submitted
          auto resourceMonitor = instance->getResourceMonitor();
          VulkanAsyncResourceCollection frameResources(resourceMonitor, (VulkanAsyncResourceHandle *)nullptr, {
            stagingBufferHandle, imageHandle,
          });
          resourceMonitor->append(move(frameResources));
        }
      }
    }

    void VulkanTexture::copyFromImage(uint32_t x, uint32_t y, uint32_t copyWidth, uint32_t copyHeight, uint32_t layerIndex, uint32_t level, VkCommandBuffer transferCommandBuffer, bool wait)
    {
      auto copyCommandBuffer = transferCommandBuffer;

      if(!size || !stagingBuffer)
      {
        verr << "Vulkan Warning:  Attempting to call VulkanTexture::copyFromImage() with invalid staging buffer!" << endl;
      }
      if(transferCommandBuffer && wait)
      {
        verr << "Vulkan Warning:  Cannot honor VulkanTexture::copyFromImage()'s wait=true when a transferCommandBuffer is supplied" << endl;
        wait = false;
      }

      if(!transferCommandBuffer)
      {
        copyCommandBuffer = startOneTimeCommandBuffer();
      }

      VkBufferImageCopy region = {};
      region.bufferOffset = (size / numArrayLayers) * layerIndex;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;

      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = level;
      region.imageSubresource.baseArrayLayer = layerIndex;
      region.imageSubresource.layerCount = 1;

      region.imageOffset = { (int32_t)x, (int32_t)y, 0 };
      region.imageExtent = { copyWidth, copyHeight, 1 };

      if(!isSwapchainImage)
      {
        transitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layerIndex, copyCommandBuffer);
        vkCmdCopyImageToBuffer(copyCommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);
        transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerIndex, copyCommandBuffer);
      }
      else
      {
        transitionLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layerIndex, copyCommandBuffer);
        vkCmdCopyImageToBuffer(copyCommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);
        transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, layerIndex, copyCommandBuffer);
      }

      if(!transferCommandBuffer)
      {
        //submit and wait
        submitOneTimeCommandBuffer(copyCommandBuffer, true);
      }
      else if(instance->getCurrentTransferCommandBuffer().first)
      {
        bool frame = false;

#ifndef VGL_VULKAN_CORE_STANDALONE
        auto csm = static_cast<vgl::CoreStateMachine *>(instance->getParentRenderer());

        if(csm->isInsideFrame())
          frame = true;
#endif

        if(frame)
        {
          uint64_t frameId = instance->getSwapChain()->getCurrentFrameId();
          auto resourceMonitor = instance->getResourceMonitor();

          VulkanAsyncResourceCollection frameResources(resourceMonitor, frameId, {
            stagingBufferHandle, imageHandle,
          });
          resourceMonitor->append(move(frameResources));
        }
        else
        {
          //by passing a null fence, we mark these resources as committed to a command buffer that hasn't
          //been submitted yet, and the fence will be provided when the transfer buffer is finally submitted
          auto resourceMonitor = instance->getResourceMonitor();
          VulkanAsyncResourceCollection frameResources(resourceMonitor, (VulkanAsyncResourceHandle *)nullptr, {
            stagingBufferHandle, imageHandle,
          });
          resourceMonitor->append(move(frameResources));
        }
      }
    }

    void VulkanTexture::setDeferImageCreation(bool defer)
    {
      deferImageCreation = defer;
    }

    void VulkanTexture::retainResourcesUntilFrameCompletion(uint64_t frameId)
    {
      auto resourceMonitor = instance->getResourceMonitor();
      auto handles = (stagingBufferHandle) ? vector<VulkanAsyncResourceHandle *>{ stagingBufferHandle, samplerHandle, imageHandle } 
                                           : vector<VulkanAsyncResourceHandle *>{ samplerHandle, imageHandle };

      VulkanAsyncResourceCollection frameResources(resourceMonitor, frameId, handles);
      resourceMonitor->append(move(frameResources));
    }

#ifndef VGL_VULKAN_CORE_STANDALONE
    void VulkanTexture::bind(int binding)
    {
      auto csm = static_cast<vgl::CoreStateMachine *>(instance->getParentRenderer());

      if(type != TT_CUBE_MAP)
        csm->setTextureBinding2D(this, binding);
      else
        csm->setCubemapBinding(this, binding);
    }

    void VulkanTexture::safeUnbind()
    {
      if(auto csm = static_cast<vgl::CoreStateMachine *>(instance->getParentRenderer()))
      {
        csm->unsetTextureBinding(this);
      }
    }
#else
    void VulkanTexture::safeUnbind(int binding)
    {
      //route this to your renderer if you persist texture bindings in your descriptor sets
    }
#endif
  }
}
