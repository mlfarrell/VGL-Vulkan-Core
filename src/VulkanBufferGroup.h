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
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"

namespace vgl
{
  namespace core
  {
    class VulkanInstance;

    ///The idea behind vgl core buffer groups is that they are lower level facilities
    ///for hinting at how to manage memory allocations for multiple "grouped" buffers
    ///The higher level vgl::BufferArray abstraction will use this class directly
    class VulkanBufferGroup
    {
    public:
      VulkanBufferGroup(VkDevice device, VkCommandPool commandPool, VkQueue queue, int numBuffers);
      ~VulkanBufferGroup();

      ///Returns the underlying vulkan buffer
      VkBuffer get(int bufferIndex);

      enum UsageType { UT_VERTEX=0, UT_INDEX, UT_UNIFORM, UT_UNIFORM_DYNAMIC, UT_SHADER_STORAGE };
      inline void setUsageType(UsageType ut) { usageType = ut; }

      inline int getNumBuffers() { return bufferCount; }
      inline size_t getSize(int bufferIndex) { return buffers[bufferIndex].size; }

      ///If set to true, this buffer will prefer device local memory for its final storage location
      ///This must be set BEFORE data() is called to have an effect
      void setPreferredDeviceLocal(bool local);

      ///Returns whether or not the buffer returned by get() is device local or not
      bool isDeviceLocal();

      ///True if this texture is currently resident on the device
      inline bool isDeviceResident() { return isResident; }
      
      ///Tentative API for auto-reclaiming staging memory after used by data().
      ///Only has an effect on device local buffers & non-persistently mapped buffers.
      ///Disabling can save some CPU overhead if you're calling data() very frequently.
      ///Currently, the default behavior is true
      void setAutomaticallyReleaseStagingMemory(bool b);

      ///If this is true, the entire buffer group's memory allocation will be come from this one dedicated allocation
      ///For now, this is required to be set if persistent mapping is desired.  This must be set
      ///BEFORE data() is called to have an effect.  For now, this only effects the host-visible memory.
      ///Dedicated allocations should be reserved for limited occuring things such as Dynamc Uniform Buffers
      void setDedicatedAllocation(bool dedicated, size_t allocationSize);

      ///When using this buffer as a shader resource, it must be included in a descriptor set
      void putDescriptor(int bufferIndex, VkDescriptorSet set, uint32_t binding, VkDeviceSize offset=0, 
        VkDeviceSize range=VK_WHOLE_SIZE, uint32_t arrayElement=0);
      void putDescriptor(VkWriteDescriptorSet &write, VkDescriptorBufferInfo &bufferInfo, int bufferIndex,
                         VkDescriptorSet set, uint32_t binding, VkDeviceSize offset=0, VkDeviceSize range=VK_WHOLE_SIZE,
                         uint32_t arrayElement=0);
      
      ///Supplies data to a buffer via first mapping to staging buffer, then copying to device.  
      ///Pass a non-null command buffer to use it (instead of creating one from command pool) to transfer.  
      ///Pass frame=true to attach resources to the current framebuffer/swapchain frame
      void data(int bufferIndex, const void *data, size_t numBytes, VkCommandBuffer transferCommandBuffer=nullptr, bool frame=false);
      
      ///Gets data back from a buffer by copying it to host memory (exposing only this for now instead of direct mapped ptr access)
      ///readback must be explicitly enabled prior to calling data() before this can be used.
      void readData(int bufferIndex, void *data, size_t numBytes);

      ///Readback must be enabled before data() if getData() will be called on this buffer group
      void setReadbackEnabled(bool enabled);

      ///Copies data into this buffer from another buffer
      void copyData(VulkanBufferGroup *srcGroup, int srcBufferIndex, int targetBufferIndex, size_t sizeInBytes, 
        VkCommandBuffer transferCommandBuffer=nullptr, bool frame=false);
      
      ///Binds a buffer from this group for use as an index buffer for indexed-rendering 
      void bindIndices(int bufferIndex, VkCommandBuffer graphicsCommandBuffer, bool shortIndices = false, VkDeviceSize offset=0);

      ///The first call to this function will persistently map the buffer until its desttuction.  
      ///Subsequent calls will simply return the mapped address
      void *getPersistentlyMappedAddress(int bufferIndex);

      ///Explicitly retains the async resource handles this class manages for the given frame id (you should never need to call this)
      void retainResourcesUntilFrameCompletion(uint64_t frameId);

      ///If this is true, the dedicated host allocation does not automatically make its updates visible to the GPU (you must flush)
      inline bool getHostAllocationNeedsFlush() { return hostAllocationNeedsFlush; }

      ///This only has an affect if the given allocation is host visible AND non coherent (needs to be flushed)
      void flush(int bufferIndex);

      ///It won't be necessary to manually call these methods for most circumstances
      void pipelineWriteBarrier(int bufferIndex, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkCommandBuffer transferCommandBuffer);
      void pipelineReadBarrier(int bufferIndex, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkCommandBuffer transferCommandBuffer);
      
    protected:
      VulkanInstance *instance;
      VkDevice device;
      VkCommandPool commandPool;
      VkQueue queue;

      struct PerBuffer
      {
        VulkanAsyncResourceHandle *bufferHandle, *stagingBufferHandle;
        VkBuffer buffer, stagingBuffer;
        VulkanMemoryManager::Suballocation bufferAllocation, stagingBufferAllocation;
        size_t size;
        void *persistentlyMappedAddress;
      };
      PerBuffer *buffers;

      bool stageToDevice = true, stagingBufferDeviceLocal = false;
      bool autoReleaseStaging = true;
      bool readbackEnabled = false;
      bool isResident = false;
      int bufferCount;
      UsageType usageType = UT_VERTEX;
      uint64_t allocationId = 0, dedicatedHostAllocationId = 0;

      bool dedicatedAllocation = false, hostAllocationNeedsFlush = false;
      size_t dedicatedAllocationSize = 0, dedicatedAllocationEnd = 0;
      VkMemoryPropertyFlagBits dedicatedHostAllocationMemoryFlags;
      void *persistentlyMappedHostMemoryAddress = nullptr;

      void copyFromStaging(int bufferIndex, VkCommandBuffer transferCommandBuffer);
      void copyToStaging(int bufferIndex, VkCommandBuffer transferCommandBuffer);
      void copyFromBuffer(VulkanBufferGroup *srcBufferGroup, int srcBufferIndex, int destBufferIndex, VkCommandBuffer transferCommandBuffer);
              
      void releaseStagingBuffers();
    };

    typedef VulkanBufferGroup BufferGroup;
  }
}
