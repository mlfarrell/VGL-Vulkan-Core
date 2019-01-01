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
#include "VulkanInstance.h"
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
      instance = &VulkanInstance::currentInstance();
      if(device == VK_NULL_HANDLE)
        device = instance->getDefaultDevice();
      buffers = new PerBuffer[numBuffers];
      memset(buffers, 0, sizeof(PerBuffer)*numBuffers);
    }

    VulkanBufferGroup::~VulkanBufferGroup()
    {
      auto &mm = *instance->getMemoryManager();

      for(int i = 0; i < bufferCount; i++)
      {
        if(buffers[i].buffer)
        {
          if(buffers[i].bufferHandle->release())
            delete buffers[i].bufferHandle;
        }
        if(buffers[i].stagingBuffer)
        {
          if(persistentlyMappedHostMemoryAddress)
          {
            vkUnmapMemory(device, buffers[i].stagingBufferAllocation.memory);
            persistentlyMappedHostMemoryAddress = nullptr;
          }

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

    void VulkanBufferGroup::setPreferredDeviceLocal(bool local)
    {
      stageToDevice = local;
    }

    bool VulkanBufferGroup::isDeviceLocal()
    {
      return (stageToDevice || stagingBufferDeviceLocal);
    }

    void VulkanBufferGroup::setDedicatedAllocation(bool dedicated, size_t allocationSize)
    {
      dedicatedAllocation = dedicated;
      dedicatedAllocationSize = allocationSize;
    }

    void VulkanBufferGroup::putDescriptor(int bufferIndex, VkDescriptorSet set, uint32_t binding, VkDeviceSize offset,
      VkDeviceSize range, uint32_t arrayElement)
    {
      VkWriteDescriptorSet write = {};
      VkDescriptorBufferInfo bufferInfo = {};

      bufferInfo.buffer = get(bufferIndex);
      bufferInfo.offset = 0;
      bufferInfo.range = range;

      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = set;
      write.dstBinding = binding;
      write.dstArrayElement = arrayElement;

      switch(usageType)
      {
        case UT_UNIFORM: write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
        case UT_UNIFORM_DYNAMIC: write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; break;
        default:  break;
      }
      write.descriptorCount = 1;

      write.pBufferInfo = &bufferInfo;
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void VulkanBufferGroup::data(int bufferIndex, const void *data, size_t numBytes, VkCommandBuffer transferCommandBuffer, bool frame)
    {
      auto memoryManager = instance->getMemoryManager();
      bool emptyBuffer = false;

      //if we try to create a zero-sized buffer, vulkan will choke
      if(numBytes == 0)
      {
        numBytes = 1;
        emptyBuffer = true;
      }

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
        else if(usageType == UT_UNIFORM || usageType == UT_UNIFORM_DYNAMIC)
          bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      };

      VkBufferCreateInfo bufferInfo = {};
      VkMemoryRequirements memRequirements;
      VulkanMemoryManager::Suballocation alloc;

      if(!buffers[bufferIndex].stagingBuffer)
      {
        auto stagingBufferMemoryFlags = (dedicatedHostAllocationId) ? dedicatedHostAllocationMemoryFlags : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = numBytes;
        if(!stageToDevice)
          setFinalBufferUsage(bufferInfo);
        else
          bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].stagingBuffer) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Failed to create vertex buffer!");
        }
        vkGetBufferMemoryRequirements(device, buffers[bufferIndex].stagingBuffer, &memRequirements);

        if(dedicatedAllocation && !dedicatedHostAllocationId)
        {
          //this is likely for something performance critical such as dynamic UBOs, so lets aim for best possible memory type
          //in this case, we purpose the staging buffer as both the device local & host visible allocation for this buffer
          stagingBufferMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
          auto dedicatedAlloc = memoryManager->allocateDedicated(memRequirements.memoryTypeBits, stagingBufferMemoryFlags, (VkDeviceSize)dedicatedAllocationSize);

          if(dedicatedAlloc.first)
          {
            hostAllocationNeedsFlush = !memoryManager->isAllocationCoherent(dedicatedAlloc.second);
            stagingBufferDeviceLocal = true;
          }
          else
          {
            //..and if we cannot get it, fall back on host coherent memory
            stagingBufferMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            dedicatedAlloc = memoryManager->allocateDedicated(memRequirements.memoryTypeBits, stagingBufferMemoryFlags, (VkDeviceSize)dedicatedAllocationSize);
            stagingBufferDeviceLocal = false;
          }

          if(!dedicatedAlloc.first)
          {
            //TODO:  better handle alloc failure...
            throw vgl_runtime_error("Failed to allocate Vulkan buffer dedicated region.");
          }

          dedicatedHostAllocationId = dedicatedAlloc.second;
          dedicatedHostAllocationMemoryFlags = (VkMemoryPropertyFlagBits)stagingBufferMemoryFlags;
        }

        alloc = memoryManager->allocate(memRequirements.memoryTypeBits,
          stagingBufferMemoryFlags, memRequirements.size, memRequirements.alignment, false, dedicatedHostAllocationId);
        buffers[bufferIndex].stagingBufferAllocation = alloc;

        vkBindBufferMemory(device, buffers[bufferIndex].stagingBuffer, alloc.memory, alloc.offset);
        buffers[bufferIndex].stagingBufferHandle = VulkanAsyncResourceHandle::newBuffer(instance->getResourceMonitor(), device, buffers[bufferIndex].stagingBuffer, alloc);
        buffers[bufferIndex].persistentlyMappedAddress = nullptr;
      }

      if(stageToDevice && !buffers[bufferIndex].buffer)
      {
        setFinalBufferUsage(bufferInfo);
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].buffer) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Failed to create vertex buffer!");
        }

        vkGetBufferMemoryRequirements(device, buffers[bufferIndex].buffer, &memRequirements);

        alloc = memoryManager->allocate(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          memRequirements.size, memRequirements.alignment);
        if(!alloc)
        {
          //must be out of GPU memory, fallback on whater we can use
          alloc = memoryManager->allocate(memRequirements.memoryTypeBits, 0,
            memRequirements.size, memRequirements.alignment);
        }
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
          verr << "Vulkan Performance Warning:  buffer group allocations fall across suballocation boundary!" << endl;
        }

        vkBindBufferMemory(device, buffers[bufferIndex].buffer, alloc.memory, alloc.offset);
        buffers[bufferIndex].bufferHandle = VulkanAsyncResourceHandle::newBuffer(instance->getResourceMonitor(), device, buffers[bufferIndex].buffer, alloc);
      }

      buffers[bufferIndex].size = numBytes;

      if(data)
      {
        alloc = buffers[bufferIndex].stagingBufferAllocation;
        void *mappedPtr;
        if(vkMapMemory(device, alloc.memory, alloc.offset, buffers[bufferIndex].size, 0, &mappedPtr) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
        }
        if(!emptyBuffer)
          memcpy(mappedPtr, data, numBytes);
        vkUnmapMemory(device, buffers[bufferIndex].stagingBufferAllocation.memory);

        if(frame)
        {
          uint64_t frame = instance->getSwapChain()->getCurrentFrameId();
          auto resourceMonitor = instance->getResourceMonitor();
          VulkanAsyncResourceCollection frameResources(resourceMonitor, frame, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            });
          resourceMonitor->append(move(frameResources));
        }
        else if(instance->getCurrentTransferCommandBuffer().first)
        {
          //by passing a null fence, we mark these resources as committed to a command buffer that hasn't
          //been submitted yet, and the fence will be provided when the transfer buffer is finally submitted
          auto resourceMonitor = instance->getResourceMonitor();
          VulkanAsyncResourceCollection frameResources(resourceMonitor, (VulkanAsyncResourceHandle *)nullptr, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            });
          resourceMonitor->append(move(frameResources));
        }

        if(stageToDevice)
          copyToStaging(bufferIndex, transferCommandBuffer);
      }
    }

    void VulkanBufferGroup::copyDataFrom(VulkanBufferGroup *srcGroup, int srcBufferIndex, int bufferIndex, size_t numBytes,
      VkCommandBuffer transferCommandBuffer, bool frame)
    {
      data(bufferIndex, nullptr, numBytes, transferCommandBuffer, frame);

      if(stageToDevice)
        copyFrom(srcGroup, srcBufferIndex, bufferIndex, transferCommandBuffer);
      else
        throw vgl_runtime_error("Currently, VulkanBufferGroup::copyDataFrom() only works for device local buffers!");
    }

    void VulkanBufferGroup::bindIndices(int bufferIndex, VkCommandBuffer graphicsCommandBuffer, bool shortIndices, VkDeviceSize offset)
    {
      VkIndexType indexType = (!shortIndices) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
      vkCmdBindIndexBuffer(graphicsCommandBuffer, get(bufferIndex), offset, indexType);
    }

    void *VulkanBufferGroup::getPersistentlyMappedAddress(int bufferIndex)
    {
      if(!dedicatedAllocation)
        throw vgl_runtime_error("Cannot use VulkanBufferGroup::getPersistentlyMappedAddress() on a non dedicated buffer resource");

      auto alloc = buffers[bufferIndex].stagingBufferAllocation;
      if(!persistentlyMappedHostMemoryAddress)
      {
        if(vkMapMemory(device, alloc.memory, 0, dedicatedAllocationSize, 0, &persistentlyMappedHostMemoryAddress) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::getPersistentlyMappedAddress()!");
        }        
      }

      //storing this part may not be necessary anymore
      /*if(!buffers[bufferIndex].persistentlyMappedAddress)
      {
        buffers[bufferIndex].persistentlyMappedAddress = 
      }*/

      return (uint8_t *)persistentlyMappedHostMemoryAddress+alloc.offset;
    }

    void VulkanBufferGroup::retainResourcesUntilFrameCompletion(uint64_t frameId)
    {
      vector<VulkanAsyncResourceHandle *> handles;

      for(int i = 0; i < bufferCount; i++)
      {
        if(buffers[i].buffer)
          handles.push_back(buffers[i].bufferHandle);
        if(buffers[i].stagingBuffer)
          handles.push_back(buffers[i].stagingBufferHandle);
      }

      auto resourceMonitor = instance->getResourceMonitor();
      VulkanAsyncResourceCollection transferResources(resourceMonitor, frameId, handles);
      resourceMonitor->append(move(transferResources));
    }

    void VulkanBufferGroup::flush(int bufferIndex)
    {
      //currently, only persistently mapped buffers could be non coherent
      if(hostAllocationNeedsFlush && persistentlyMappedHostMemoryAddress)
      {
        auto alloc = buffers[bufferIndex].stagingBufferAllocation;
        //if(vkMapMemory(device, alloc.memory, 0, dedicatedAllocationSize, 0, &persistentlyMappedHostMemoryAddress) != VK_SUCCESS)

        VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        range.memory = alloc.memory;
        range.offset = alloc.offset;
        range.size = instance->getMemoryManager()->getAllocationSize(alloc);

        vkFlushMappedMemoryRanges(device, 1, &range);
      }
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
          throw vgl_runtime_error("Could not create Vulkan Fence!");

        vkEndCommandBuffer(copyCommandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCommandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, transferFence);

        //just a note, fences must always be the LAST entry in these collections
        auto resourceMonitor = instance->getResourceMonitor();
        auto fenceHandle = VulkanAsyncResourceHandle::newFence(resourceMonitor, device, transferFence);
        auto cmdBufHandle = VulkanAsyncResourceHandle::newCommandBuffer(resourceMonitor, device, copyCommandBuffer, commandPool);
        VulkanAsyncResourceCollection transferResources(resourceMonitor, fenceHandle, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            cmdBufHandle, fenceHandle
          });
        resourceMonitor->append(move(transferResources));
        fenceHandle->release();
        cmdBufHandle->release();
      }
    }

    void VulkanBufferGroup::copyFrom(VulkanBufferGroup *srcBufferGroup, int srcBufferIndex, int bufferIndex, VkCommandBuffer transferCommandBuffer)
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
      vkCmdCopyBuffer(copyCommandBuffer, srcBufferGroup->buffers[srcBufferIndex].stagingBuffer, buffers[bufferIndex].buffer, 1, &copyRegion);

      if(!transferCommandBuffer)
      {
        VkFence transferFence;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        if(vkCreateFence(device, &fenceInfo, nullptr, &transferFence) != VK_SUCCESS)
          throw vgl_runtime_error("Could not create Vulkan Fence!");

        vkEndCommandBuffer(copyCommandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &copyCommandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, transferFence);

        //just a note, fences must always be the LAST entry in these collections
        auto resourceMonitor = instance->getResourceMonitor();
        auto fenceHandle = VulkanAsyncResourceHandle::newFence(resourceMonitor, device, transferFence);
        auto cmdBufHandle = VulkanAsyncResourceHandle::newCommandBuffer(resourceMonitor, device, copyCommandBuffer, commandPool);
        VulkanAsyncResourceCollection transferResources(resourceMonitor, fenceHandle, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            cmdBufHandle, fenceHandle
          });
        resourceMonitor->append(move(transferResources));
        fenceHandle->release();
        cmdBufHandle->release();
      }
    }
  }
}
