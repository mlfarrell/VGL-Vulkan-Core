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
#include "VulkanInstance.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"

namespace vgl
{
  namespace core
  {
    class VulkanTexture
    {
    public:
      enum TextureType { TT_1D=0, TT_2D, TT_CUBE_MAP };

      VulkanTexture(TextureType type);
      VulkanTexture(VulkanSwapChain *swapchain, int imageIndex);
      VulkanTexture(VkDevice device, TextureType type, VkCommandPool commandPool, VkQueue queue);
      ~VulkanTexture();

      VkImage get();

      ///When using this texture as a shader resource, it must be included in a descriptor set
      void putDescriptor(VkDescriptorSet set, uint32_t binding, uint32_t arrayElement=0);

      //this has nothing to do with textures, get this out of here!
      //void copyDescriptor(VkDescriptorSet srcSet, VkDescriptorSet dstSet, uint32_t srcBinding, uint32_t dstBinding);

      ///Supplies data to a texture via first mapping to staging buffer, then copying to device
      ///Pass a non-null command buffer to use it (instead of creating one from command pool) to transfer
      void imageData(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, const void *data, size_t numBytes, uint32_t layerIndex=0, uint32_t numSamples=1, VkCommandBuffer transferCommandBuffer=nullptr);

      ///Initializes the texture image similarly to imageData() but instead leaves the image contents undefined
      enum Usage : uint32_t
      {
        U_SAMPLED_IMAGE=1, 
        U_DEPTH_STENCIL_ATTACHMENT=2,
        U_COLOR_ATTACHMENT=4
      };
      void initImage(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, uint32_t usage=U_SAMPLED_IMAGE, uint32_t layerIndex=0, uint32_t numSamples = 1, VkCommandBuffer transferCommandBuffer=nullptr);

      void readImageData(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t layer, void *data);

      enum SamplerFilterType { ST_LINEAR, ST_NEAREST, ST_LINEAR_MIPMAP_LINEAR, ST_LINEAR_MIPMAP_NEAREST };
      void setFilters(SamplerFilterType minFilter, SamplerFilterType magFilter);

      void setAnisotropicFiltering(bool enabled, int maxSamples);

      enum WrapMode { WM_UNDEFINED = 0, WM_REPEAT, WM_CLAMP, WM_MIRRORED_REPEAT };
      void setWrapMode(WrapMode mode);

      ///Mipmapping has to be enabled BEFORE imageData is called for it to have an effect
      void setMipmap(bool enabled, float lodBias=0);

      void transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerIndex, VkCommandBuffer transferCommandBuffer);
      void transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer transferCommandBuffer);

      inline VkFormat getFormat() { return format; }
      inline VkImageView getImageView() { return imageView; }
      inline VkExtent2D getDimensions() { return { width, height }; }
      inline VkSampleCountFlagBits getMultiSamples() { return (VkSampleCountFlagBits)numMultiSamples; }

      ///True if this texture is currently resident on the device
      inline bool isDeviceResident() { return isResident; }

      ///True if this texture was created with the notion that it would be used for sampled images inside shaders
      inline bool isShaderResource() { return isShaderRsrc; }

      inline bool isUndefined() { return (stagingBufferHandle == nullptr && imageHandle == nullptr); }

      ///If enabled, actuaul GPU texture image will not be created until this texture is used in a render 
      ///(has to be enabled BEFORE imageData is called for it to have an effect)
      void setDeferImageCreation(bool defer);

#ifndef VGL_VULKAN_CORE_STANDALONE
      void bind(int binding);
#endif

    protected:
      VulkanInstance *instance;
      VkDevice device;
      VkCommandPool commandPool;
      VkQueue queue;

      VulkanAsyncResourceHandle *imageHandle = nullptr, *stagingBufferHandle = nullptr, *samplerHandle = nullptr;
      VkImage image = VK_NULL_HANDLE;
      VkBuffer stagingBuffer = VK_NULL_HANDLE;
      VkImageView imageView = VK_NULL_HANDLE;
      VkSampler sampler = VK_NULL_HANDLE;
      VulkanMemoryManager::Suballocation imageAllocation = nullptr, stagingBufferAllocation = nullptr;
      VkFormat format = VK_FORMAT_UNDEFINED;
      size_t size = 0;
      uint32_t width = 0, height = 0, depth = 1;
      uint32_t numMipLevels = 0, numArrayLayers = 0;
      uint32_t numMultiSamples = 1;
      bool deferImageCreation = false;
      bool isDepth = false, isStencil = false, isShaderRsrc = false, isSwapchainImage = false, stagingBufferTransferDst = false;
      bool isResident = false;

      SamplerFilterType minFilter = ST_LINEAR, magFilter = ST_LINEAR;
      VkSamplerCreateInfo samplerState;
      bool mipmapEnabled = false;
      bool samplerDirty = true;

      int bufferCount;
      TextureType type;

      VkCommandBuffer startOneTimeCommandBuffer();
      void submitOneTimeCommandBuffer(VkCommandBuffer commandBuffer, bool wait=false);

      void createStagingBuffer(bool needsTransferDest);
      void createImage();
      void createImageView();
      void createSampler();
      void copyToImage(uint32_t layerIndex, VkCommandBuffer transferCommandBuffer);
      void copyFromImage(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t layerIndex, VkCommandBuffer transferCommandBuffer, bool wait);
      void generateMipmaps(uint32_t layerIndex, VkCommandBuffer transferCommandBuffer);
    };

    typedef VulkanTexture Texture;
  }
}