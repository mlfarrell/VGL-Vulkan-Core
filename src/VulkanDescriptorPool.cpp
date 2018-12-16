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
#include <vector>
#include "VulkanInstance.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanAsyncResourceHandle.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanDescriptorPool::VulkanDescriptorPool(VkDevice device, uint32_t maxSets, uint32_t numUbos, uint32_t numDynamicUbos, uint32_t numCombinedSamplers, uint32_t numInputAttachments)
      : device(device), maxSets(maxSets), numUbos(numUbos), numDynamicUbos(numDynamicUbos), numCombinedSamplers(numCombinedSamplers), 
        numInputAttachments(numInputAttachments)
    {
      VkDescriptorPoolSize poolSize = {};
      vector<VkDescriptorPoolSize> sizes;
      sizes.reserve(6);

      if(numUbos > 0)
      {
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = numUbos;
        sizes.push_back(poolSize);
      }
      if(numDynamicUbos > 0)
      {
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSize.descriptorCount = numDynamicUbos;
        sizes.push_back(poolSize);
      }
      if(numCombinedSamplers > 0)
      {
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = numCombinedSamplers;
        sizes.push_back(poolSize);
      }
      if(numInputAttachments > 0)
      {
        poolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        poolSize.descriptorCount = numInputAttachments;
        sizes.push_back(poolSize);
      }

      VkDescriptorPoolCreateInfo poolInfo = {};
      poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      poolInfo.poolSizeCount = (uint32_t)sizes.size();
      poolInfo.pPoolSizes = sizes.data();
      poolInfo.maxSets = maxSets;

      if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan descriptor pool!");
    }

    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDescriptorPool *fromPool, uint32_t sizeMultiplier)
      : VulkanDescriptorPool(fromPool->device, sizeMultiplier*fromPool->maxSets, sizeMultiplier*fromPool->numUbos, 
        sizeMultiplier*fromPool->numDynamicUbos, sizeMultiplier*fromPool->numCombinedSamplers, sizeMultiplier*fromPool->numInputAttachments)
    {
      if(DebugBuild())
      {
        vout << "Creating new descriptor pool of size (" << maxSets << ", " << numUbos << ", " << numCombinedSamplers << 
          ") mid-frame likely due to pool overflow" << endl;
      }
    }

    VulkanDescriptorPool::VulkanDescriptorPool(VkDevice device, VkDescriptorPool pool)
      : device(device), pool(pool)
    {
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
      if(pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool, nullptr);
    }

    bool VulkanDescriptorPool::simpleAllocate(uint32_t count, VulkanDescriptorSetLayout *layout, VkDescriptorSet *outSets)
    {
      vector<VkDescriptorSetLayout> layouts(count, layout->get());

      VkDescriptorSetAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = pool;
      allocInfo.descriptorSetCount = count;
      allocInfo.pSetLayouts = layouts.data();

      return (vkAllocateDescriptorSets(device, &allocInfo, outSets) == VK_SUCCESS);
    }

    VulkanAsyncResourceHandle *VulkanDescriptorPool::asyncReleaseOwnership()
    {
      auto instance = &VulkanInstance::currentInstance();
      auto handle = VulkanAsyncResourceHandle::newDescriptorPool(instance->getResourceMonitor(), device, pool);

      pool = VK_NULL_HANDLE;
      return handle;
    }
  }
}