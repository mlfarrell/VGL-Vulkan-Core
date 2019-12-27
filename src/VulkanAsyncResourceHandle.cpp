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
#include <stdexcept>
#include <chrono>
#include <thread>
#include "VulkanAsyncResourceHandle.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "System.h"
#endif

#ifdef max
#undef max
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    static const size_t spinupCapacity = 512;

    VulkanAsyncResourceCollection::VulkanAsyncResourceCollection(VulkanAsyncResourceMonitor *monitor, VulkanAsyncResourceHandle *fence, const std::vector<struct VulkanAsyncResourceHandle *> &handles)
      : monitor(monitor), frame(false)
    {
      this->fence = fence;
      this->handles = handles;

      if(fence)
        fence->retain();
    }

    VulkanAsyncResourceCollection::VulkanAsyncResourceCollection(VulkanAsyncResourceMonitor *monitor, uint64_t frameId, const std::vector<struct VulkanAsyncResourceHandle *> &handles)
      : monitor(monitor), frame(true)
    {
      this->frameId = frameId;
      this->handles = handles;
    }

    bool VulkanAsyncResourceCollection::check(VkDevice device)
    {
      VkResult status = VK_SUCCESS;

      if(!frame)
      {
        if(!fence)
        {
          return false;
        }
        status = vkGetFenceStatus(device, fence->fence);
      }
      else
      {
        status = (monitor->completedFrame.load() > frameId) ? VK_SUCCESS : VK_NOT_READY;
      }

      if(status == VK_SUCCESS)
      {
        for(auto &handle : handles) if(handle)
        {
          if(handle->release())          
          {
            delete handle;
            handle = nullptr;
          }
        }
        if(!frame)
        {
          if(fence->release())
          {
            delete fence;
            fence = nullptr;
          }
        }
        return true;
      }
      else if(status == VK_ERROR_DEVICE_LOST)
      {
        throw std::runtime_error("VulkanAsyncResourceFence::checkFence encountered VK_ERROR_DEVICE_LOST");
      }

      return false;
    }

    bool VulkanAsyncResourceCollection::wait(VkDevice device)
    {
      VkResult status = VK_SUCCESS;
      auto busyWaitFrame = [this] {
        while(monitor->completedFrame.load() < frameId)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return VK_SUCCESS;
      };

      if(!frame)
        status = vkWaitForFences(device, 1, &fence->fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
      else
        status = busyWaitFrame();

      if(status == VK_SUCCESS)
      {
        for(auto &handle : handles) if(handle)
        {
          if(handle->release())
          {
            delete handle;
            handle = nullptr;
          }
        }
        if(!frame)
        {
          if(fence->release())
          {
            delete fence;
            fence = nullptr;
          }
        }
        return true;
      }
      else
      {
        throw std::runtime_error("VulkanAsyncResourceFence::waitFence encountered unknown error");
      }

      return false;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    VulkanAsyncResourceHandle::VulkanAsyncResourceHandle(VulkanAsyncResourceMonitor *monitor, Type type, VkDevice device)
      : monitor(monitor), device(device), type(type)
    {
      refCount = 1;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newBuffer(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkBuffer buffer, VulkanMemoryManager::Suballocation alloc)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, BUFFER, device);

      handle->buffer = buffer;
      handle->alloc = alloc;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newImage(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkImage image, VkImageView imageView, VulkanMemoryManager::Suballocation alloc)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, IMAGE, device);

      handle->image = image;
      handle->imageView = imageView;
      handle->alloc = alloc;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newSampler(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkSampler sampler)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, SAMPLER, device);

      handle->sampler = sampler;
      handle->alloc = nullptr;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newCommandBuffer(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkCommandBuffer commandBuffer, VkCommandPool pool)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, COMMAND_BUFFER, device);

      handle->commandBuffer = commandBuffer;
      handle->commandPool = pool;
      handle->alloc = nullptr;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newFence(VulkanAsyncResourceMonitor *monitor, VkDevice device, VkFence fence)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, FENCE, device);

      handle->fence = fence;
      handle->alloc = nullptr;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newDescriptorPool(VulkanAsyncResourceMonitor * monitor, VkDevice device, VkDescriptorPool pool)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, DESCRIPTOR_POOL, device);

      handle->descriptorPool = pool;
      handle->alloc = nullptr;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newFunction(VulkanAsyncResourceMonitor *monitor, VkDevice device, std::function<void()> func)
    {
      auto handle = new VulkanAsyncResourceHandle(monitor, FUNCTION, device);

      handle->function = new std::function<void()>(func);
      handle->alloc = nullptr;
      return handle;
    }

    void VulkanAsyncResourceHandle::retain()
    {
      refCount++;
    }

    bool VulkanAsyncResourceHandle::release()
    {
      refCount--;
      if(refCount.load() == 0)
      {
        dealloc();
        return true;
      }

      return false;
    }

    void VulkanAsyncResourceHandle::dealloc()
    {
      switch(type)
      {
        case FENCE: 
          vkDestroyFence(device, fence, nullptr);
        break;
        case BUFFER: 
          vkDestroyBuffer(device, buffer, nullptr);
        break;
        case IMAGE:
          vkDestroyImage(device, image, nullptr);
          if(imageView)
            vkDestroyImageView(device, imageView, nullptr);
        break;
        case SAMPLER:
          vkDestroySampler(device, sampler, nullptr);
        break;
        case DESCRIPTOR_POOL:
          vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        break;
        case COMMAND_BUFFER:
        {
#ifndef VGL_VULKAN_CORE_STANDALONE
          //keep an eye on this performance-wise
          auto dv = device;
          auto cp = commandPool;
          auto cb = commandBuffer;

          System::system().scheduleTask([dv, cp, cb] {
            vkFreeCommandBuffers(dv, cp, 1, &cb);
          });
#else
          vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
#endif
        }
        break;
        case FUNCTION:
        {
#ifndef VGL_VULKAN_CORE_STANDALONE
          auto f = function;
          System::system().scheduleTask([f] {
            (*f)();
            delete f;
          });
#else
          //if you need this to be called from the main thread, then you'll need to adapt this to whatever scheduling engine you're using here
          auto f = function;
          (*f)();
          delete f;
#endif
        }
        break;
      }

      if(alloc)
      {
        monitor->memoryManager->free(alloc);
      }
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    VulkanAsyncResourceMonitor::VulkanAsyncResourceMonitor(VulkanMemoryManager *memoryManager)
      : memoryManager(memoryManager)
    {
      completedFrame = 0;
    }

    void VulkanAsyncResourceMonitor::append(VulkanAsyncResourceCollection &&collection, bool commitNullFences)
    {
      std::lock_guard<std::mutex> locker(lock);

      //lets not screw around here
      if(resourceCollections.size() < spinupCapacity || resourceCollections.capacity() < spinupCapacity)
        resourceCollections.reserve(spinupCapacity);

      if(commitNullFences)
      {
        for(auto &rc : resourceCollections) if(!rc.frame && rc.fence == nullptr)
        {
          rc.fence = collection.fence;
          rc.fence->retain();
        }
      }

      resourceCollections.emplace_back(collection);
      for(auto handle : resourceCollections.back().handles) if(handle)
        handle->retain();
      
      VGL_CORE_PERF_WARNING_DEBUG(resourceCollections.size() > 1024,
                                  "VGL Performance Warning:  Async resource collections array has grown to a large size "
                                  "(likely too many buffer updates per frame)!")
    }

    void VulkanAsyncResourceMonitor::poll(VkDevice device)
    {
      std::lock_guard<std::mutex> locker(lock);

      auto it = resourceCollections.begin();
      while(it != resourceCollections.end())
      {
        auto &collection = *it;

        if(collection.check(device))
          it = resourceCollections.erase(it);
        else
          it++;
      }
    }

    void VulkanAsyncResourceMonitor::wait(VkDevice device)
    {
      std::lock_guard<std::mutex> locker(lock);

      for(auto &collection : resourceCollections)
      {
        collection.wait(device);
      }

      //we'd still need to check for completed frame ids here (poll)
      throw vgl_runtime_error("Unfinished method called");

      resourceCollections.clear();
    }

    VulkanAsyncResourceMonitor::~VulkanAsyncResourceMonitor()
    {
      std::lock_guard<std::mutex> locker(lock);

      for(auto &collection : resourceCollections)
      {
        for(auto &handle : collection.handles) if(handle)
        {
          //ugh..
          if(handle->type == VulkanAsyncResourceHandle::FUNCTION)
          {
            (*(handle->function))();
            delete handle->function;
            delete handle;
            handle = nullptr;
            continue;
          }

          if(handle->release())
          {
            delete handle;
            handle = nullptr;
          }
        }
        if(!collection.frame && collection.fence)
        {
          if(collection.fence->release())
          {
            delete collection.fence;
          }
        }
      }

      resourceCollections.clear();
    }
  }
}
