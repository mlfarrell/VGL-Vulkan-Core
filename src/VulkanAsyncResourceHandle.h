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

#include <atomic>
#include <vector>
#include <mutex>
#include <functional>
#include "vulkan.h"
#include "VulkanMemoryManager.h"

///The concepts in this file will evolve and mature over time...

namespace vgl
{
  namespace core
  {
    struct VulkanAsyncResourceMonitor;
    struct VulkanAsyncResourceHandle;

    struct VulkanAsyncResourceCollection
    {
      union
      {
        VulkanAsyncResourceHandle *fence;
        uint64_t frameId;
      };

      std::vector<struct VulkanAsyncResourceHandle *> handles;
      VulkanAsyncResourceMonitor *monitor;

      //this struct either uses a fence or a frame # to determine if the command buffer using the resource has completed
      bool frame;

      VulkanAsyncResourceCollection(VulkanAsyncResourceMonitor *monitor, VulkanAsyncResourceHandle *fence, const std::vector<struct VulkanAsyncResourceHandle *> &handles);
      VulkanAsyncResourceCollection(VulkanAsyncResourceMonitor *monitor, uint64_t frameId, const std::vector<struct VulkanAsyncResourceHandle *> &handles);
      bool check(VkDevice device);

      ///Do not call wait from the main rendering thread!
      bool wait(VkDevice device);
    };

    ///This class is likely going to live in a background thread eventually
    struct VulkanAsyncResourceMonitor
    {
      VulkanAsyncResourceMonitor(VulkanMemoryManager *memoryManager);
      ~VulkanAsyncResourceMonitor();

      void append(VulkanAsyncResourceCollection &&collection, bool commitNullFences=false);
      void poll(VkDevice device);
      void wait(VkDevice device);
      inline void setCompletedFrame(uint64_t frame) { completedFrame = frame; }

      std::vector<VulkanAsyncResourceCollection> resourceCollections;
      VulkanMemoryManager *memoryManager;

      //This is the highest completed frame # (frame id)
      std::atomic_uint64_t completedFrame;
      std::mutex lock;
    };

    struct VulkanAsyncResourceHandle
    {
      union
      {
        VkFence fence;
        VkBuffer buffer;
        VkImage image;
        VkSampler sampler;
        VkCommandBuffer commandBuffer;
        VkDescriptorPool descriptorPool;
        std::function<void()> *function;
      };
      union
      {
        VkImageView imageView;
        VkCommandPool commandPool;
      };
      VulkanMemoryManager::Suballocation alloc;
      VkDevice device;

      enum Type
      {
        FENCE, BUFFER, IMAGE, SAMPLER, COMMAND_BUFFER, DESCRIPTOR_POOL, FUNCTION
      };

      VulkanAsyncResourceHandle(VulkanAsyncResourceMonitor *monitor, Type type, VkDevice device);

      static VulkanAsyncResourceHandle *newBuffer(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkBuffer buffer, VulkanMemoryManager::Suballocation alloc);
      static VulkanAsyncResourceHandle *newImage(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkImage image, VkImageView imageView, VulkanMemoryManager::Suballocation alloc);
      static VulkanAsyncResourceHandle *newSampler(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkSampler sampler);
      static VulkanAsyncResourceHandle *newCommandBuffer(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkCommandBuffer commandBuffer, VkCommandPool pool);
      static VulkanAsyncResourceHandle *newFence(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkFence fence);
      static VulkanAsyncResourceHandle *newDescriptorPool(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkDescriptorPool pool);

      ///The nuclear option:  Unlike the others, this one will call the given function on the main thread when the resource is to be freed
      static VulkanAsyncResourceHandle *newFunction(VulkanAsyncResourceMonitor *monitor, VkDevice device, std::function<void()> function);

      void retain();
      bool release();
      void dealloc();

      VulkanAsyncResourceMonitor *monitor;
      Type type;
      std::atomic_int refCount;
    };
  }
}