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

namespace vgl
{
  namespace core
  {
    class VulkanDescriptorSetLayout;
    struct VulkanAsyncResourceHandle;

    class VulkanDescriptorPool
    {
    public:
      ///Creates a new descriptor pool suitable for storing the explicitly given number of descriptor types
      VulkanDescriptorPool(VkDevice device, uint32_t maxSets, uint32_t numUbos, uint32_t numDynamicUbos, uint32_t numCombinedSamplers, 
        uint32_t numInputAttachments);

      ///Creates a new descriptor pool from an existing descriptor pool's base counts, applying a sizing multipler to create a larger pool
      VulkanDescriptorPool(VulkanDescriptorPool *fromPool, uint32_t sizeMultiplier);

      ///Creates a descriptor pool class that wraps an already created low-level Vulkan descriptor pool object.  
      ///(Cannot be duplicated or expanded using the size multiplier method)
      VulkanDescriptorPool(VkDevice device, VkDescriptorPool pool);

      ~VulkanDescriptorPool();

      ///Attempts to allocate 1 or many descriptor sets from the underlying pool, returning false if unable
      bool simpleAllocate(uint32_t count, VulkanDescriptorSetLayout *layout, VkDescriptorSet *outSets);

      ///Releases the ownership of the underlying descriptor pool to the returned new async resource handle
      VulkanAsyncResourceHandle *asyncReleaseOwnership();

      ///Accesses the underlying Vulkan descriptor pool object
      inline VkDescriptorPool get() { return pool; }

    protected:
      VkDevice device;
      VkDescriptorPool pool;

      uint32_t maxSets=0, numUbos=0, numDynamicUbos=0, numCombinedSamplers=0, numInputAttachments=0;
    };
  }
}