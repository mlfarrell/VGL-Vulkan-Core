#include "pch.h"
#include <stdexcept>
#include <chrono>
#include "VulkanAsyncResourceHandle.h"

#ifdef max //FUCK YOU windows
#undef max
#endif

namespace vgl
{
  namespace core
  {
    std::vector<VulkanAsyncResourceCollection> VulkanAsyncResourceMonitor::resourceCollections;
    std::atomic_uint64_t VulkanAsyncResourceMonitor::completedFrame = 0;
    static VulkanMemoryManager *mm = nullptr;

    VulkanAsyncResourceCollection::VulkanAsyncResourceCollection(VkFence fence, const std::vector<struct VulkanAsyncResourceHandle *> &handles)
      : frame(false)
    {
      this->fence = fence;
      this->handles = handles;
    }

    VulkanAsyncResourceCollection::VulkanAsyncResourceCollection(uint64_t frameId, const std::vector<struct VulkanAsyncResourceHandle *> &handles)
      : frame(true)
    {
      this->frameId = frameId;
      this->handles = handles;
    }

    bool VulkanAsyncResourceCollection::check(VkDevice device)
    {
      VkResult status = VK_SUCCESS;

      if(!frame)
        status = vkGetFenceStatus(device, fence);
      else
        status = (VulkanAsyncResourceMonitor::completedFrame.load() >= frameId) ? VK_SUCCESS : VK_NOT_READY;

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
        while(VulkanAsyncResourceMonitor::completedFrame.load() < frameId)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return VK_SUCCESS;
      };

      if(!frame)
        status = vkWaitForFences(device, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
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
        return true;
      }
      else
      {
        throw std::runtime_error("VulkanAsyncResourceFence::waitFence encountered unknown error");
      }

      return false;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    VulkanAsyncResourceHandle::VulkanAsyncResourceHandle(Type type, VkDevice device)
      : device(device), type(type)
    {
      refCount = 1;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newBuffer(VkDevice device, VkBuffer buffer, VulkanMemoryManager::Suballocation alloc)
    {
      auto handle = new VulkanAsyncResourceHandle(BUFFER, device);

      handle->buffer = buffer;
      handle->alloc = alloc;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newCommandBuffer(VkDevice device, VkCommandBuffer commandBuffer, VkCommandPool pool)
    {
      auto handle = new VulkanAsyncResourceHandle(COMMAND_BUFFER, device);

      handle->commandBuffer = commandBuffer;
      handle->commandPool = pool;
      handle->alloc = nullptr;
      return handle;
    }

    VulkanAsyncResourceHandle *VulkanAsyncResourceHandle::newFence(VkDevice device, VkFence fence)
    {
      auto handle = new VulkanAsyncResourceHandle(FENCE, device);

      handle->fence = fence;
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
        case COMMAND_BUFFER:
          vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        break;
      }

      if(alloc)
      {
        if(!mm)
          mm = &VulkanMemoryManager::manager();
        mm->free(alloc);
      }
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void VulkanAsyncResourceMonitor::append(VulkanAsyncResourceCollection &&collection)
    {
      resourceCollections.emplace_back(collection);
      for(auto handle : resourceCollections.back().handles) if(handle)
        handle->retain();
    }

    void VulkanAsyncResourceMonitor::poll(VkDevice device)
    {
      auto it = resourceCollections.begin();
      while(it !=  resourceCollections.end())
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
      for(auto &collection : resourceCollections)
      {
        collection.wait(device);
      }

      //we'd still need to check for completed frame ids here (poll)

      resourceCollections.clear();
    }

    void VulkanAsyncResourceMonitor::cleanup()
    {
      for(auto &collection : resourceCollections)
      {
        for(auto &handle : collection.handles) if(handle)
        {
          if(handle->release())
          {
            delete handle;
            handle = nullptr;
          }
        }
      }

      resourceCollections.clear();
    }
  }
}