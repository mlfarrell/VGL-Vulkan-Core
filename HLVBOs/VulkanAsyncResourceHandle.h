#pragma once

#include <atomic>
#include <vector>
#include "vulkan.h"
#include "VulkanMemoryManager.h"

///The concepts in this file will evolve and mature over time...

namespace vgl
{
  namespace core
  {
    struct VulkanAsyncResourceCollection
    {
      union
      {
        VkFence fence;
        uint64_t frameId;
      };

      std::vector<struct VulkanAsyncResourceHandle *> handles;

      //this struct either uses a fence or a frame # to determine if the command buffer using the resource has completed
      bool frame;

      VulkanAsyncResourceCollection(VkFence fence, const std::vector<struct VulkanAsyncResourceHandle *> &handles);
      VulkanAsyncResourceCollection(uint64_t frameId, const std::vector<struct VulkanAsyncResourceHandle *> &handles);
      bool check(VkDevice device);

      ///Do not call wait from the main rendering thread!
      bool wait(VkDevice device);
    };

    ///This class is likely going to live in a background thread eventually
    struct VulkanAsyncResourceMonitor
    {
      static void append(VulkanAsyncResourceCollection &&collection);
      static void poll(VkDevice device);
      static void wait(VkDevice device);
      static void cleanup();
      static inline void setCompletedFrame(uint64_t frame) { completedFrame = frame; }

      static std::vector<VulkanAsyncResourceCollection> resourceCollections;

      //This is the highest completed frame # (frame id)
      static std::atomic_uint64_t completedFrame;
    };

    struct VulkanAsyncResourceHandle
    {
      union
      {
        VkFence fence;
        VkBuffer buffer;
        VkCommandBuffer commandBuffer;
      };
      VulkanMemoryManager::Suballocation alloc;
      VkCommandPool commandPool;
      VkDevice device;

      enum Type
      {
        FENCE, BUFFER, COMMAND_BUFFER
      };

      VulkanAsyncResourceHandle(Type type, VkDevice device);

      static VulkanAsyncResourceHandle *newBuffer(VkDevice device, VkBuffer buffer, VulkanMemoryManager::Suballocation alloc);
      static VulkanAsyncResourceHandle *newCommandBuffer(VkDevice device, VkCommandBuffer commandBuffer, VkCommandPool pool);
      static VulkanAsyncResourceHandle *newFence(VkDevice device, VkFence fence);

      void retain();
      bool release();
      void dealloc();

      Type type;
      std::atomic_int refCount;
    };
  }
}