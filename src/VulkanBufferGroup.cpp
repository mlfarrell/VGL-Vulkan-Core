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
            auto alloc = buffers[i].stagingBufferAllocation;           
            auto allocInfo = instance->getMemoryManager()->getAllocationInfo(alloc);

            vkUnmapMemory(device, allocInfo.memory);
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

      putDescriptor(write, bufferInfo, bufferIndex, set, binding, offset, range, arrayElement);
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    
    void VulkanBufferGroup::putDescriptor(VkWriteDescriptorSet &write, VkDescriptorBufferInfo &bufferInfo, int bufferIndex,
                                          VkDescriptorSet set, uint32_t binding, VkDeviceSize offset, VkDeviceSize range,
                                          uint32_t arrayElement)
    {
      bufferInfo.buffer = get(bufferIndex);
      bufferInfo.offset = 0;
      bufferInfo.range = range;
      
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = set;
      write.dstBinding = binding;
      write.dstArrayElement = arrayElement;
      write.pNext = nullptr;
      write.pImageInfo = nullptr;
      write.pTexelBufferView = nullptr;
      
      switch(usageType)
      {
        case UT_UNIFORM: write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
        case UT_UNIFORM_DYNAMIC: write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; break;
        case UT_SHADER_STORAGE: write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
        default:  break;
      }
      write.descriptorCount = 1;
      
      write.pBufferInfo = &bufferInfo;
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
        buffers[bufferIndex].buffer = VK_NULL_HANDLE;
      }
      if(buffers[bufferIndex].stagingBuffer)
      {
        if(buffers[bufferIndex].stagingBufferHandle->release())
          delete buffers[bufferIndex].stagingBufferHandle;
        buffers[bufferIndex].stagingBuffer = VK_NULL_HANDLE;
      }

      auto setFinalBufferUsage = [=](VkBufferCreateInfo &bufferInfo) {
        if(usageType == UT_VERTEX)
          bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        else if(usageType == UT_INDEX)
          bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        else if(usageType == UT_UNIFORM || usageType == UT_UNIFORM_DYNAMIC)
          bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        else if(usageType == UT_SHADER_STORAGE)
          bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      };

      VkBufferCreateInfo bufferInfo = {};
      VulkanMemoryManager::Suballocation alloc;

      if(!buffers[bufferIndex].stagingBuffer)
      {
        VkMemoryRequirements memRequirements;

        auto stagingBufferMemoryFlags = (dedicatedHostAllocationId) ? dedicatedHostAllocationMemoryFlags : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = numBytes;
        if(!stageToDevice)
          setFinalBufferUsage(bufferInfo);
        else
          bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        
        if(readbackEnabled)
          bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].stagingBuffer) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Failed to create vertex buffer!");
        }

        if(dedicatedAllocation)
          vkGetBufferMemoryRequirements(device, buffers[bufferIndex].stagingBuffer, &memRequirements);

        if(dedicatedAllocation && !dedicatedHostAllocationId)
        {
          //this is likely for something performance critical such as dynamic UBOs, so lets aim for best possible memory type
          //in this case, we purpose the staging buffer as both the device local & host visible allocation for this buffer
          stagingBufferMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
          auto dedicatedAlloc = memoryManager->allocateDedicated(memRequirements.memoryTypeBits, stagingBufferMemoryFlags, 
            (VkDeviceSize)dedicatedAllocationSize, 1);

          if(dedicatedAlloc.first)
          {
            hostAllocationNeedsFlush = !memoryManager->isAllocationTypeCoherent(memRequirements.memoryTypeBits, stagingBufferMemoryFlags);
            stagingBufferDeviceLocal = true;
            isResident = true;
          }
          else
          {
            //..and if we cannot get it, fall back on host coherent memory
            stagingBufferMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            dedicatedAlloc = memoryManager->allocateDedicated(memRequirements.memoryTypeBits, stagingBufferMemoryFlags,
              (VkDeviceSize)dedicatedAllocationSize, 1);
            stagingBufferDeviceLocal = false;
            isResident = false;
          }

          if(!dedicatedAlloc.first)
          {
            //TODO:  better handle alloc failure...
            throw vgl_runtime_error("Failed to allocate Vulkan buffer dedicated region.");
          }

          dedicatedHostAllocationId = dedicatedAlloc.second;
          dedicatedHostAllocationMemoryFlags = (VkMemoryPropertyFlagBits)stagingBufferMemoryFlags;
        }

        alloc = memoryManager->allocateBuffer(stagingBufferMemoryFlags, buffers[bufferIndex].stagingBuffer, dedicatedHostAllocationId);

        buffers[bufferIndex].stagingBufferAllocation = alloc;
        memoryManager->bindBufferMemory(buffers[bufferIndex].stagingBuffer, alloc);
        buffers[bufferIndex].stagingBufferHandle = VulkanAsyncResourceHandle::newBuffer(instance->getResourceMonitor(), device, buffers[bufferIndex].stagingBuffer, alloc);          
        buffers[bufferIndex].persistentlyMappedAddress = nullptr;
        
        if(!alloc)
          throw vgl_runtime_error("Failed to allocate vulkan buffer!");
      }

      if(stageToDevice && !buffers[bufferIndex].buffer)
      {
        setFinalBufferUsage(bufferInfo);
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        if(readbackEnabled)
          bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        
        if(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[bufferIndex].buffer) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Failed to create vertex buffer!");
        }

        alloc = memoryManager->allocateBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffers[bufferIndex].buffer);
        if(!alloc)
        {
          //must be out of GPU memory, fallback on whatever we can use
          alloc = memoryManager->allocateBuffer(0, buffers[bufferIndex].buffer);
          isResident = false;
          
          if(!alloc)
            throw vgl_runtime_error("Failed to allocate vulkan buffer!");
        }
        else
        {
          isResident = true;
        }
        buffers[bufferIndex].bufferAllocation = alloc;

        memoryManager->bindBufferMemory(buffers[bufferIndex].buffer, alloc);
        buffers[bufferIndex].bufferHandle = VulkanAsyncResourceHandle::newBuffer(instance->getResourceMonitor(), device, buffers[bufferIndex].buffer, alloc);
      }

      buffers[bufferIndex].size = numBytes;

      if(data)
      {
        alloc = buffers[bufferIndex].stagingBufferAllocation;

        auto allocInfo = memoryManager->getAllocationInfo(alloc);
        void *mappedPtr;
        if(vkMapMemory(device, allocInfo.memory, allocInfo.offset, buffers[bufferIndex].size, 0, &mappedPtr) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
        }
        if(!emptyBuffer)
          memcpy(mappedPtr, data, numBytes);
        vkUnmapMemory(device, allocInfo.memory);

        if(frame)
        {
          uint64_t frame = instance->getSwapChain()->getCurrentFrameId();
          auto resourceMonitor = instance->getResourceMonitor();
          VulkanAsyncResourceCollection frameResources(resourceMonitor, frame, {
            buffers[bufferIndex].stagingBufferHandle, buffers[bufferIndex].bufferHandle,
            });
          resourceMonitor->append(move(frameResources));
        }
        else if(instance->getCurrentTransferCommandBuffer())
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
          copyFromStaging(bufferIndex, transferCommandBuffer);
      }
      
      if(autoReleaseStaging)
        releaseStagingBuffers();
    }
  
    void VulkanBufferGroup::setAutomaticallyReleaseStagingMemory(bool b)
    {
      autoReleaseStaging = b;
    }

    void VulkanBufferGroup::releaseStagingBuffers()
    {
      if(!stageToDevice)
        return;
      
      for(int i = 0; i < bufferCount; i++)
      {
        if(buffers[i].stagingBuffer)
        {
          if(!persistentlyMappedHostMemoryAddress)
          {
            if(buffers[i].stagingBufferHandle->release())
              delete buffers[i].stagingBufferHandle;
            buffers[i].stagingBuffer = VK_NULL_HANDLE;
            buffers[i].stagingBufferHandle = nullptr;
          }
        }
      }
    }
  
    void VulkanBufferGroup::setReadbackEnabled(bool enabled)
    {
      readbackEnabled = enabled;
      autoReleaseStaging = false;
    }
  
    void VulkanBufferGroup::readData(int bufferIndex, void *data, size_t numBytes)
    {
      if(!readbackEnabled)
        throw vgl_runtime_error("You cannot call VulkanBufferGroup::readData() if readback has not been explicitly enabled!");
      
      if(stageToDevice)
      {
        if(!commandPool)
        {
          commandPool = instance->getTransferCommandPool();
          queue = instance->getGraphicsQueue();
        }
        copyToStaging(bufferIndex, VK_NULL_HANDLE);
      }
      
      auto memoryManager = instance->getMemoryManager();
      auto alloc = buffers[bufferIndex].stagingBufferAllocation;
      auto allocInfo = memoryManager->getAllocationInfo(alloc);
      void *mappedPtr;
      if(vkMapMemory(device, allocInfo.memory, allocInfo.offset, buffers[bufferIndex].size, 0, &mappedPtr) != VK_SUCCESS)
        throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::data()!");
      if(numBytes)
        memcpy(data, mappedPtr, numBytes);
      vkUnmapMemory(device, allocInfo.memory);
    }

    void VulkanBufferGroup::copyData(VulkanBufferGroup *srcGroup, int srcBufferIndex, int bufferIndex, size_t numBytes,
      VkCommandBuffer transferCommandBuffer, bool frame)
    {
      data(bufferIndex, nullptr, numBytes, transferCommandBuffer, frame);

      if(stageToDevice)
        copyFromBuffer(srcGroup, srcBufferIndex, bufferIndex, transferCommandBuffer);
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

      auto memoryManager = instance->getMemoryManager();
      auto alloc = buffers[bufferIndex].stagingBufferAllocation;
      auto allocInfo = memoryManager->getAllocationInfo(alloc);

      if(!persistentlyMappedHostMemoryAddress)
      {
        if(vkMapMemory(device, allocInfo.memory, 0, dedicatedAllocationSize, 0, &persistentlyMappedHostMemoryAddress) != VK_SUCCESS)
        {
          throw vgl_runtime_error("Unable to map staging buffer in VulkanBufferGroup::getPersistentlyMappedAddress()!");
        }        
      }

      //only using this feature for dedicated for now
      return (uint8_t *)persistentlyMappedHostMemoryAddress+allocInfo.offset;
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
        auto memoryManager = instance->getMemoryManager();
        auto alloc = buffers[bufferIndex].stagingBufferAllocation;
        auto allocInfo = memoryManager->getAllocationInfo(alloc);

        VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        range.memory = allocInfo.memory;
        range.offset = allocInfo.offset;
        range.size = allocInfo.size;

        vkFlushMappedMemoryRanges(device, 1, &range);
      }
    }

    void VulkanBufferGroup::copyFromStaging(int bufferIndex, VkCommandBuffer transferCommandBuffer)
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
      
      pipelineWriteBarrier(bufferIndex, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, copyCommandBuffer);

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
      vkCmdCopyBuffer(copyCommandBuffer, buffers[bufferIndex].buffer, buffers[bufferIndex].stagingBuffer, 1, &copyRegion);
      
      pipelineWriteBarrier(bufferIndex, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, copyCommandBuffer);

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
        
        vkWaitForFences(device, 1, &transferFence, VK_TRUE, numeric_limits<uint64_t>::max());
      }
    }

    void VulkanBufferGroup::copyFromBuffer(VulkanBufferGroup *srcBufferGroup, int srcBufferIndex, int bufferIndex, VkCommandBuffer transferCommandBuffer)
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
      
      if(srcBufferGroup->buffers[srcBufferIndex].stagingBuffer)
      {
        VkBufferCopy copyRegion = {};
        copyRegion.size = buffers[bufferIndex].size;
        vkCmdCopyBuffer(copyCommandBuffer, srcBufferGroup->buffers[srcBufferIndex].stagingBuffer, buffers[bufferIndex].buffer, 1, &copyRegion);
      }
      else
      {
        srcBufferGroup->pipelineReadBarrier(srcBufferIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, copyCommandBuffer);
        
        VkBufferCopy copyRegion = {};
        copyRegion.size = buffers[bufferIndex].size;
        vkCmdCopyBuffer(copyCommandBuffer, srcBufferGroup->buffers[srcBufferIndex].buffer, buffers[bufferIndex].buffer, 1, &copyRegion);
      }
      
      pipelineWriteBarrier(bufferIndex, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, copyCommandBuffer);

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
  
    void VulkanBufferGroup::pipelineWriteBarrier(int bufferIndex, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkCommandBuffer transferCommandBuffer)
    {
      VkBufferMemoryBarrier barrier = {};
      VkPipelineStageFlags sourceStage = srcStage;
      VkPipelineStageFlags destinationStage = dstStage;

      barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      barrier.buffer = buffers[bufferIndex].buffer;
      barrier.size = VK_WHOLE_SIZE;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      if(srcStage != VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      else
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      
      switch(usageType)
      {
        case UT_INDEX:
          barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
          destinationStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        break;
        case UT_VERTEX:
          barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
          destinationStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        break;
        case UT_UNIFORM:
        case UT_UNIFORM_DYNAMIC:
          barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
          destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        break;
        case UT_SHADER_STORAGE:
          barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;
      }
      
      if(dstStage == VK_PIPELINE_STAGE_TRANSFER_BIT)
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      else if(dstStage == VK_PIPELINE_STAGE_HOST_BIT)
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      
      vkCmdPipelineBarrier(transferCommandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
  
    void VulkanBufferGroup::pipelineReadBarrier(int bufferIndex, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkCommandBuffer transferCommandBuffer)
    {
      VkBufferMemoryBarrier barrier = {};
      VkPipelineStageFlags sourceStage = srcStage;
      VkPipelineStageFlags destinationStage = dstStage;

      barrier.buffer = buffers[bufferIndex].buffer;
      barrier.size = VK_WHOLE_SIZE;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      
      vkCmdPipelineBarrier(transferCommandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
  }
}
