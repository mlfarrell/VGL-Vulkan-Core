#pragma once

#include <vector>
#include "vulkan.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"

namespace vgl
{
  namespace core
  {
    ///The idea behind vgl core buffer groups is that they are lower level facilities
    ///for hinting at how to manage memory allocations for multiple "grouped" buffers
    ///The higher level vgl::BufferArray abstraction will use this class directly
    class VulkanBufferGroup
    {
    public:
      VulkanBufferGroup(VkDevice device, VkCommandPool commandPool, VkQueue queue, int numBuffers);
      ~VulkanBufferGroup();

      VkBuffer get(int bufferIndex);

      enum UsageType { UT_VERTEX=0, UT_INDEX, UT_UNIFORM };
      inline void setUsageType(UsageType ut) { usageType = ut; }

      void setStaging(bool staging);

      ///Supplies data to a buffer via first mapping to staging buffer, then copying to device
      ///Pass a non-null command buffer to use it (instead of creating one from command pool) to transfer
      ///Pass frame=true to attach resources to the current framebuffer/swapchain frame
      void data(int bufferIndex, const void *data, size_t numBytes, VkCommandBuffer transferCommandBuffer=nullptr, bool frame=false);

      ///Binds a buffer from this group for use as an index buffer for indexed-rendering 
      void bindIndices(int bufferIndex, VkCommandBuffer graphicsCommandBuffer, bool shortIndices = false, VkDeviceSize offset=0);

    protected:
      VkDevice device;
      VkCommandPool commandPool;
      VkQueue queue;

      struct PerBuffer
      {
        VulkanAsyncResourceHandle *bufferHandle, *stagingBufferHandle;
        VkBuffer buffer, stagingBuffer;
        VulkanMemoryManager::Suballocation bufferAllocation, stagingBufferAllocation;
        size_t size;
      };
      PerBuffer *buffers;

      bool stageToDevice = true;
      int bufferCount;
      UsageType usageType = UT_VERTEX;
      uint64_t allocationId = 0;

      void copyToStaging(int bufferIndex, VkCommandBuffer transferCommandBuffer);
    };
  }
}