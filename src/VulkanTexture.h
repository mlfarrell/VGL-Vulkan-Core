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
      void putDescriptor(VkWriteDescriptorSet &write, VkDescriptorImageInfo &imageInfo, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement=0);

      //this has nothing to do with textures, get this out of here!
      //void copyDescriptor(VkDescriptorSet srcSet, VkDescriptorSet dstSet, uint32_t srcBinding, uint32_t dstBinding);

      ///Supplies data to a texture via first mapping to staging buffer, then copying to device
      ///Pass a non-null command buffer to use it (instead of creating one from command pool) to transfer
      void imageData(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, const void *data, size_t numBytes, uint32_t layerIndex=0, uint32_t level=0, uint32_t numSamples=1, VkCommandBuffer transferCommandBuffer=nullptr);

      ///Tentative API for auto-reclaiming staging memory after used by imageData().
      ///Disabling can save some CPU overhead & heap-dirtying if you're calling imageData() very frequently.
      ///Currently, the default behavior is true
      void setAutomaticallyReleaseStagingMemory(bool b);

      ///Initializes the texture image similarly to imageData() but instead leaves the image contents undefined
      enum Usage : uint32_t
      {
        U_SAMPLED_IMAGE=1, 
        U_DEPTH_STENCIL_ATTACHMENT=2,
        U_COLOR_ATTACHMENT=4
      };
      void initImage(uint32_t width, uint32_t height, uint32_t depth, VkFormat format, uint32_t usage=U_SAMPLED_IMAGE, uint32_t layerIndex=0, uint32_t numSamples = 1, VkCommandBuffer transferCommandBuffer=nullptr);

      void readImageData(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t layer, uint32_t level, void *data);
      
      enum SamplerFilterType { ST_LINEAR, ST_NEAREST, ST_LINEAR_MIPMAP_LINEAR, ST_LINEAR_MIPMAP_NEAREST };
      void setFilters(SamplerFilterType minFilter, SamplerFilterType magFilter);
      
      enum CompareMode
      {
         CM_NEVER = 0,
         CM_LESS = 1,
         CM_EQUAL = 2,
         CM_LESS_OR_EQUAL = 3,
         CM_GREATER = 4,
         CM_NOT_EQUAL = 5,
         CM_GREATER_OR_EQUAL = 6,
         CM_ALWAYS = 7,
      };
      void setCompareMode(CompareMode mode, bool enabled=true);

      void setAnisotropicFiltering(bool enabled, int maxSamples);

      enum WrapMode { WM_UNDEFINED = 0, WM_REPEAT, WM_CLAMP, WM_MIRRORED_REPEAT };
      void setWrapMode(WrapMode mode);
      void setWrapMode(WrapMode uMode, WrapMode vMode, WrapMode wMode);

      ///Mipmapping has to be enabled BEFORE imageData() is called for it to have an effect
      void setMipmap(bool enabled, float lodBias=0);

      ///Readback must be enabled before initImage() if readImageData() will be called on this texture
      void setReadbackEnabled(bool enabled);

      void transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerIndex, VkCommandBuffer transferCommandBuffer);
      void transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer transferCommandBuffer);

      inline VkFormat getFormat() { return format; }
      inline VkImageView getImageView() { return imageView; }
      inline VkSampler getSampler();
      inline VkExtent2D getDimensions() { return { width, height }; }
      inline VkSampleCountFlagBits getMultiSamples() { return (VkSampleCountFlagBits)numMultiSamples; }
      inline TextureType getType() { return type; }

      ///True if this texture is currently resident on the device
      inline bool isDeviceResident() { return isResident; }

      ///True if this texture was created with the notion that it would be used for sampled images inside shaders
      inline bool isShaderResource() { return isShaderRsrc; }

      inline bool isUndefined() { return (stagingBufferHandle == nullptr && imageHandle == nullptr); }
      
      inline void setAutoGenerateMipmaps(bool b) { autoGenerateMipmaps = b; }

      ///If enabled, actuaul GPU texture image will not be created until this texture is used in a render 
      ///(has to be enabled BEFORE imageData is called for it to have an effect)
      void setDeferImageCreation(bool defer);

      ///Explicitly retains the async resource handles this class manages for the given frame id (you should never need to call this)
      void retainResourcesUntilFrameCompletion(uint64_t frameId);

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
      bool autoGenerateMipmaps = true;
      bool deferImageCreation = false;
      bool isDepth = false, isStencil = false, isShaderRsrc = false, isSwapchainImage = false, stagingBufferTransferDst = false;
      bool isResident = false;
      bool autoReleaseStaging = true;

      SamplerFilterType minFilter = ST_LINEAR, magFilter = ST_LINEAR;
      VkSamplerCreateInfo samplerState;
      bool mipmapEnabled = false, readbackEnabled = false;
      bool samplerDirty = true;

      int bufferCount;
      TextureType type;

      VkCommandBuffer startOneTimeCommandBuffer();
      void submitOneTimeCommandBuffer(VkCommandBuffer commandBuffer, bool wait=false);

      void createStagingBuffer(bool needsTransferDest);
      void createImage();
      void createImageView();
      void createSampler();
      void copyToImage(uint32_t layerIndex, uint32_t level, VkCommandBuffer transferCommandBuffer);
      void copyFromImage(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t layerIndex, uint32_t level, VkCommandBuffer transferCommandBuffer, bool wait);
      void generateMipmaps(uint32_t layerIndex, VkCommandBuffer transferCommandBuffer);

      void releaseStagingBuffers();

      void safeUnbind();
    };

    typedef VulkanTexture Texture;
  }
}
