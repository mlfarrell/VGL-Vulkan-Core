
#include "pch.h"
#include <iostream>
#include "VulkanBufferGroup.h"
#include "VulkanMemoryManager.h"
#include "VulkanAsyncResourceHandle.h"
#include "VulkanFrameBuffer.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanBufferGroup::VulkanBufferGroup(VkDevice device, VkCommandPool commandPool, VkQueue queue, int numBuffers)
      : device(device), commandPool(commandPool), queue(queue), bufferCount(numBuffers)
    {
      buffers = new PerBuffer[numBuffers];
      memset(buffers, 0, sizeof(PerBuffer)*numBuffers);
    }

    VulkanBufferGroup::~VulkanBufferGroup()
    {
      auto &mm = VulkanMemoryManager::manager();

      for(int i = 0; i < bufferCount; i++)
      {
        if(buffers[i].buffer)
        {
          if(buffers[i].bufferHandle->release())
            delete buffers[i].bufferHandle;
        }
        if(buffers[i].stagingBuffer)
        {
          if(buffers[i].stagingBufferHandle->release())
            delete buffers[i].stagingBufferHandle;
        }
      }
      delete []buffers;
    }

    VkBuffer VulkanBufferGroup::get(int bufferIndex)
    {
      if(stageToDevice)
        return buffers[bufferIndex].buffer;
      else
        return buffers[bufferIndex].stagingBuffer;
    }

    void VulkanBufferGroup::setStaging(bool staging)
    {
      stageToDevice = staging;
    }

    void VulkanBufferGroup::data(int bufferIndex, const void *data, size_t numBytes, VkCommandBuffer transferCommandBuffer, bool frame)
    {
      if(buffers[bufferIndex].buffer)
      {
        if(buffers[bufferIndex].bufferHandle->release())
          delete buffers[bufferIndex].bufferHandle;
        buffers[bufferIndex].buffer = nullptr;
      }
      if(buffers[bufferIndex].stagingBuffer)
      {
        if(buffers[bufferIndex].stagingBufferHandle->release())
          delete buffers[bufferIndex].stagingBufferHandle;
        buffers[bufferIndex].stagingBuffer = nullptr;
      }

      auto setFinalBufferUsage = [=](VkBufferCreateInfo &bufferInfo) {
        if(usageType == UT_VERTEX)
          bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        else if(usageType == UT_INDEX)
          bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        else if(usageType == UT_UNIFORM)
          bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      };

      VkBufferCreateInfo bufferInfo = {};
      VkMemoryRequirements memRequirements;
      VulkanMemoryManager::Suballocation alloc;

      if(!buffers[bufferIndex].stagingBuffer)
      {
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = numBytes;
        if(!stageToDevice)
          setFinalBufferUsage(bufferInfo);
        else
          bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].stagingBuffer) != VK_SUCCESS)
        {
          throw runtime_error("Failed to create vertex buffer!");
        }

        vkGetBufferMemoryRequirements(device, buffers[bufferIndex].stagingBuffer, &memRequirements);

        alloc = VulkanMemoryManager::manager().allocate(memRequirements.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          memRequirements.size, memRequirements.alignment);
        buffers[bufferIndex].stagingBufferAllocation = alloc;

        vkBindBufferMemory(device, buffers[bufferIndex].stagingBuffer, alloc.memory, alloc.offset);
        buffers[bufferIndex].stagingBufferHandle = VulkanAsyncResourceHandle::newBuffer(device, buffers[bufferIndex].stagingBuffer, alloc);
      }

      if(stageToDevice && !buffers[bufferIndex].buffer)
      {
        setFinalBufferUsage(bufferInfo);
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].buffer) != VK_SUCCESS)
        {
          throw runtime_error("Failed to create vertex buffer!");
        }

        vkGetBufferMemoryRequirements(device, buffers[bufferIndex].buffer, &memRequirements);

        alloc = VulkanMemoryManager::manager().allocate(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          memRequirements.size, memRequirements.alignment);
        buffers[bufferIndex].bufferAllocation = alloc;

        if(!allocationId)
        {
          allocationId = alloc.allocationId;
        }
        else if(alloc.allocationId != allocationId)
        {
          //for performance reasons, its best to ensure all buffers in a group live within the same allocation
          //TODO:  this, defer it somehow until we know buffers are done being populated, then stage everything to
          //the same allocation
          cerr << "Vulkan Performance Warning:  buffer group allocations fall across suballocation boundary!" << endl;
        }

        vkBindBufferMemory(device, buffers[bufferIndex].buffer, alloc.memory, alloc.offset);
        buffers[bufferIndex].bufferHandle = VulkanAsyncResourceHandle::newBuffer(device, buffers[bufferIndex].buffer, alloc);
      }

      buffers[bufferIndex].size = numBytes;

      alloc = buffers[bufferIndex].stagingBufferAllocation;
      void *mappedPtr;
      if(vkMapMemory(device, alloc.memory, alloc.offset, buffers[bufferIndex].size, 0, &mappedPtr) != VK_SUCCESS)
      {
        throw runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
      }
      memcpy(mappedPtr, data, numBytes);
      vkUnmapMemory(device, buffers[bufferIndex].stagingBufferAllocation.memory);

      if(frame)
      {
        uint64_t frame = VulkanFrameBuffer::getCurrentFrameId();
        VulkanAsyncResourceCollection frameResources(frame, {
          buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
          });
        VulkanAsyncResourceMonitor::append(move(frameResources));
      }

      if(stageToDevice)
        copyToStaging(bufferIndex, transferCommandBuffer);
    }

    void VulkanBufferGroup::bindIndices(int bufferIndex, VkCommandBuffer graphicsCommandBuffer, bool shortIndices, VkDeviceSize offset)
    {
      VkIndexType indexType = (!shortIndices) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
      vkCmdBindIndexBuffer(graphicsCommandBuffer, get(bufferIndex), offset, indexType);
    }

    void VulkanBufferGroup::copyToStaging(int bufferIndex, VkCommandBuffer transferCommandBuffer)
    {
      auto copyCommandBuffer = transferCommandBuffer;

      if(!transferCommandBuffer)
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
        copyCommandBuffer = commandBuffer;
      }

      VkBufferCopy copyRegion = {};
      copyRegion.size = buffers[bufferIndex].size;
      vkCmdCopyBuffer(copyCommandBuffer, buffers[bufferIndex].stagingBuffer, buffers[bufferIndex].buffer, 1, &copyRegion);

      if(!transferCommandBuffer)
      {
        VkFence transferFence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        if(vkCreateFence(device, &fenceInfo, nullptr, &transferFence) != VK_SUCCESS)
          throw runtime_error("Could not create Vulkan Fence!");

        vkEndCommandBuffer(copyCommandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCommandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, transferFence);

        //just a note, fences must alwasy be the LAST entry in these collections
        VulkanAsyncResourceHandle *fenceHandle = VulkanAsyncResourceHandle::newFence(device, transferFence);
        VulkanAsyncResourceHandle *cmdBufHandle = VulkanAsyncResourceHandle::newCommandBuffer(device, copyCommandBuffer, commandPool);
        VulkanAsyncResourceCollection transferResources(transferFence, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            cmdBufHandle, fenceHandle
          });
        VulkanAsyncResourceMonitor::append(move(transferResources));
        fenceHandle->release();
        cmdBufHandle->release();
      }
    }
  }
}