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
#include <cassert>
#include <iostream>
#include <algorithm>
#include "VulkanMemoryManager.h"

#ifdef VGL_VULKAN_CORE_USE_VMA
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
#ifdef VGL_VULKAN_CORE_USE_VMA
    VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physicalDevice, VkDevice device)
    {
      auto instanceHasDedicatedAlloc = [] {
        const auto &enabledExt = VulkanInstance::currentInstance().getEnabledDeviceExtensions();
        auto ext1 = find_if(enabledExt.begin(), enabledExt.end(), [](const char *ext) {
          return ((string)ext == VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
        });
        auto ext2 = find_if(enabledExt.begin(), enabledExt.end(), [](const char *ext) {
          return ((string)ext == VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
        });
        
        return (ext1 != enabledExt.end() && ext2 != enabledExt.end());
      };
        
      VmaAllocatorCreateInfo allocatorInfo = {};

      allocatorInfo.physicalDevice = physicalDevice;
      allocatorInfo.device = device;
      if(instanceHasDedicatedAlloc())
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
      vmaCreateAllocator(&allocatorInfo, &allocator);

      vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
      memset(pools, 0, sizeof(VmaPool)*MaxPools);
    }

    void VulkanMemoryManager::makePool(uint64_t allocationId, uint32_t memoryTypeIndex, VkDeviceSize blockSize, size_t maxAllocations)
    {
      VmaPoolCreateInfo poolCreateInfo = {};
      poolCreateInfo.memoryTypeIndex = memoryTypeIndex;
      poolCreateInfo.blockSize = blockSize;
      poolCreateInfo.maxBlockCount = maxAllocations;

      assert(allocationId > 0);
      vmaCreatePool(allocator, &poolCreateInfo, &pools[allocationId-1]);      
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(VkMemoryPropertyFlags properties, VkBuffer buffer, uint64_t allocationId)
    {
      VulkanMemoryManager::Suballocation result;

      VmaAllocationCreateInfo allocInfo = {};
      allocInfo.requiredFlags = properties;
      if(properties == 0)
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      if(allocationId > 0)
        allocInfo.pool = pools[allocationId-1];

      result.allocationResult = vmaAllocateMemoryForBuffer(allocator, buffer, &allocInfo, &result.allocation, nullptr);
      return result;
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(VkMemoryPropertyFlags properties, VkImage image, uint64_t allocationId)
    {
      VulkanMemoryManager::Suballocation result;

      VmaAllocationCreateInfo allocInfo = {};
      allocInfo.requiredFlags = properties;
      if(properties == 0)
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      if(allocationId > 0)
        allocInfo.pool = pools[allocationId-1];

      result.allocationResult = vmaAllocateMemoryForImage(allocator, image, &allocInfo, &result.allocation, nullptr);
      return result;
    }

    pair<VmaPool, uint64_t> VulkanMemoryManager::allocateDedicated(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkDeviceSize blockSize, size_t maxBlocks, bool allowSuballocation, bool imageOptimal)
    {
      auto getFreePool = [=]() -> uint64_t {
        uint64_t poolIndex = 0;

        for(poolIndex = 0; poolIndex < MaxPools; poolIndex++)
        {
          if(!pools[poolIndex])
            return poolIndex+1;
        }

        return 0;
      };

      if(auto poolIndex = getFreePool())
      {
        uint64_t allocationId = poolIndex;
        uint32_t memoryType = findMemoryType(typeFilter, properties);

        if(memoryType != VK_MAX_MEMORY_TYPES)
        {
          makePool(allocationId, memoryType, blockSize, maxBlocks);

          //casting the pool to this since vma won't let me get memory
          return { pools[allocationId-1], allocationId };
        }
      }

      return { VK_NULL_HANDLE, 0 };
    }

    void VulkanMemoryManager::bindMemory(VkBuffer buffer, VulkanMemoryManager::Suballocation alloc)
    {
      if(alloc)
        vmaBindBufferMemory(allocator, alloc.allocation, buffer);
    }

    void VulkanMemoryManager::bindMemory(VkImage image, VulkanMemoryManager::Suballocation alloc)
    {
      if(alloc)
        vmaBindImageMemory(allocator, alloc.allocation, image);
    }

    VulkanMemoryManager::AllocationInfo VulkanMemoryManager::getAllocationInfo(Suballocation alloc)
    {
      VmaAllocationInfo vmaInfo;
      AllocationInfo result;

      vmaGetAllocationInfo(allocator, alloc.allocation, &vmaInfo);
      result.memory = vmaInfo.deviceMemory;
      result.memoryType = vmaInfo.memoryType;
      result.offset = vmaInfo.offset;
      result.size = vmaInfo.size;
      result.allocationId = 0;

      return result;
    }

    void VulkanMemoryManager::free(const Suballocation &suballocation)
    {
      vmaFreeMemory(allocator, suballocation.allocation);
    }

    bool VulkanMemoryManager::isAllocationCoherent(const Suballocation &suballocation)
    {
      auto info = getAllocationInfo(suballocation);
      return (memoryProperties.memoryTypes[info.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationTypeCoherent(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
      uint32_t memoryType = findMemoryType(typeFilter, properties);
      return (memoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? true : false;
    }

    uint32_t VulkanMemoryManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
      for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
      {
        if((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
          return i;
        }
      }

      return VK_MAX_MEMORY_TYPES;
    }

    void VulkanMemoryManager::dumpAllocationsInfo()
    {
      char *stats = nullptr;

      vout << "---VGL Vulkan core (vma) memory stats---" << endl;
      vmaBuildStatsString(allocator, &stats, VK_FALSE);
      vout << stats << endl;
      vmaFreeStatsString(allocator, stats);
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
      for(int i = 0; i < MaxPools; i++) if(pools[i])
        vmaDestroyPool(allocator, pools[i]);
      vmaDestroyAllocator(allocator);
    }
#else
    VulkanMemoryManager::AllocationStrategy VulkanMemoryManager::allocationStrategy = VulkanMemoryManager::AS_DESKTOP;
    VulkanMemoryManager::FreeMode VulkanMemoryManager::freeMode = VulkanMemoryManager::FM_MANUAL;
    VkDeviceSize VulkanMemoryManager::allocSizeBoundaries[2][3];

    static const uint32_t NO_MEMORY_TYPE = VK_MAX_MEMORY_TYPES;

    //TODO:  take this from vulkan device limits max allocations, but 8192 should be more than enough for most systems for now..
    static const int maxAllocations = 8192;

    static inline uint64_t fastCeil(uint64_t x, uint64_t y) { return (1 + ((x - 1) / y)); }

    VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physicalDevice, VkDevice device)
      : physicalDevice(physicalDevice), device(device)
    {
      //I'd rather use up the memory now, than deal with yet another group of structs going on the heap
      allocationsPool = new Allocation[maxAllocations];

      switch(allocationStrategy)
      {
        case AS_MOBILE_CONVERVATIVE:
          //these are the allocation tiers (suballocations < 512k in size go into 1 mb allocations, < 5mb into 25 mb, etc etc)
          allocSizeBoundaries[0][0] = (512<<10);  //512k
          allocSizeBoundaries[0][1] = (5<<20);    //5 mb
          allocSizeBoundaries[0][2] = (25<<20);   //25 mb

          allocSizeBoundaries[1][0] = (1<<20);    //1 mb
          allocSizeBoundaries[1][1] = (25<<20);   //25 mb
          allocSizeBoundaries[1][2] = (100<<20);  //100 mb
        break;
        case AS_DESKTOP:
          allocSizeBoundaries[0][0] = (1<<20);    //1 mb
          allocSizeBoundaries[0][1] = (25<<20);   //25 mb
          allocSizeBoundaries[0][2] = (100<<20);  //100 mb

          allocSizeBoundaries[1][0] = (10<<20);   //10 mb
          allocSizeBoundaries[1][1] = (100<<20);  //100 mb
          allocSizeBoundaries[1][2] = (500<<20);  //500 mb
        break;
      }

      vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        for(auto &allocation : allocations[i]) if(allocation->memory)
          vkFreeMemory(device, allocation->memory, nullptr);
      }
      delete []allocationsPool;
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(VkMemoryPropertyFlags properties, VkBuffer buffer, uint64_t allocationId)
    {
      VkMemoryRequirements memRequirements;
      vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

      return allocate(memRequirements.memoryTypeBits, properties, memRequirements.size, memRequirements.alignment, false, allocationId);
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(VkMemoryPropertyFlags properties, VkImage image, uint64_t allocationId)
    {
      VkMemoryRequirements memRequirements;
      vkGetImageMemoryRequirements(device, image, &memRequirements);

      return allocate(memRequirements.memoryTypeBits, properties, memRequirements.size, memRequirements.alignment, true, allocationId);
    }

    void VulkanMemoryManager::bindMemory(VkBuffer buffer, VulkanMemoryManager::Suballocation alloc)
    {
      if(alloc)
        vkBindBufferMemory(device, buffer, alloc.memory, alloc.offset);
    }

    void VulkanMemoryManager::bindMemory(VkImage image, VulkanMemoryManager::Suballocation alloc)
    {
      if(alloc)
        vkBindImageMemory(device, image, alloc.memory, alloc.offset);
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment, bool imageOptimal, uint64_t allocationId)
    {     
      lock_guard<mutex> locker(managerLock);
      if(requiredSize == 0)
        return {};

      AllocationType allocType = AT_DEDICATED;
        
      if(allocationId == 0)
      {
        allocType = AT_SMALL;

        if(requiredSize > allocSizeBoundaries[0][2])
        {
          uint32_t memoryType = findMemoryType(typeFilter, properties);

          allocType = AT_DEDICATED;
          if(memoryType != NO_MEMORY_TYPE)
            allocationId = allocateDedicated(memoryType, (VkDeviceSize)requiredSize, true, imageOptimal).second;
          else
            return {};
        }
        else if(requiredSize > allocSizeBoundaries[0][1])
        {
          allocType = AT_LARGE;
        }
        else if(requiredSize > allocSizeBoundaries[0][0])
        {
          allocType = AT_MED;
        }

        //vout << "Allocation size requested " << (requiredSize >> 10) << " kb getting back allocation type " << (int)allocType << endl;
        //vout << "Current allocation total: " << (allocatedBytes >> 20) << " mb" << endl;
      }

      uint32_t memoryType = findMemoryType(typeFilter, properties);
      if(memoryType == NO_MEMORY_TYPE)
        return {};
      Suballocation suballoc = findSuballocation(memoryType, requiredSize, requiredAlignment, allocType, imageOptimal, allocationId);

      if(!suballoc && allocationId == 0)
      {
        //have to create a new allocation
        if(!makeNewAllocation(memoryType, allocType, imageOptimal))
        {
          const uint64_t memoryTypeBit = (1ull<<memoryType);

          //we must be low on this heap, try with a smaller allocation if we can
          if(allocType == AT_LARGE && requiredSize < allocSizeBoundaries[1][(int)AT_MED])
          {
            allocType = AT_MED;
            suballoc = findSuballocation(memoryType, requiredSize, requiredAlignment, allocType, imageOptimal, allocationId);

            if(suballoc)
            {
              return suballoc;
            }

            if(!makeNewAllocation(memoryType, allocType, imageOptimal))
            {
              if((lowMemoryFlags & memoryTypeBit) == 0)
              {
                //try again after opening up larger dedicated allocations
                lowMemoryFlags |= memoryTypeBit;
              }
              else
              {
                return {};
              }
            }
          }
          else
          {
            if((lowMemoryFlags & memoryTypeBit) == 0)
            {
              //try again after opening up larger dedicated allocations
              lowMemoryFlags |= memoryTypeBit;
            }
            else
            {
              return {};
            }
          }
        }
        suballoc = findSuballocation(memoryType, requiredSize, requiredAlignment, allocType, imageOptimal, allocationId);

#ifdef DEBUG
        //this shouldn't be possible
        if(suballoc && requiredAlignment > 0 && suballoc.offset % requiredAlignment)
          throw vgl_runtime_error("VulkanMemoryManager::allocate alignment failed");

        if(!suballoc)
          cerr << "allocation failed" << endl;
#endif
      } 

      return suballoc;
    }

    void VulkanMemoryManager::free(const Suballocation &suballocation)
    {
      lock_guard<mutex> locker(managerLock);
      auto &allocMap = allocationsMap[suballocation.memoryType];
      auto ai = allocMap.find(suballocation.allocationId);
      bool freed = false;
      auto srIt = suballocation.subregionIt;

      if(ai != allocMap.end())
      {
        auto &allocation = *allocations[suballocation.memoryType][ai->second];

        if(allocation.suballocationCount > 0)
        {        
          if(suballocation.subregionId < invalidatedSubregionIds)
          {
            //we can't use the iterator in this case (yayyy)
            for(auto it = allocation.regions.begin(); it != allocation.regions.end(); it++)
            {
              if(it->id == suballocation.subregionId)
              {
                srIt = it;
                break;
              }
            }
          }

          //auto srPrev = (srIt != allocation.regions.begin()) ? prev(srIt) : allocation.regions.end();
          //auto srNext = next(srIt);
          auto sr = &(*srIt);

          if(sr->id == suballocation.subregionId)
          {
            bool anyMerged = false;

            freed = true;
            allocation.suballocationCount--;
            sr->free = true;

            //merging is causing problems and really isn't needed right now
            //drop the idea for the time being
            /*
            //can we merge consecutive regions now
            //to keep this fast, only checking left and right
            if(srNext != allocation.regions.end() && srNext->free)
            {
              mergeSubregions(allocation.regions, srIt, srNext);
              srIt = srNext;
              srPrev = (srIt != allocation.regions.begin()) ? prev(srIt) : allocation.regions.end();
              anyMerged = true;
            }
            if(srPrev != allocation.regions.end() && srPrev->free)
            {
              //this was wrong, we weren't merging left properly here
              mergeSubregions(allocation.regions, srIt, srPrev);
              anyMerged = true;
            }*/

            if(!anyMerged)
              allocation.freeRegions.insert({ sr, srIt });
            
            if(allocation.suballocationCount == 0)
            {
              //dedicated allocations are a special case and will be freed immediately on last free suballoc
              if(allocation.type == AT_DEDICATED || freeMode == FM_SYNCHRONOUS)
              {
                //this entire allocation is freed now
                vkFreeMemory(device, allocation.memory, nullptr);
                allocation.memory = VK_NULL_HANDLE;
              }
            }
          }
        }          
      }

#ifdef DEBUG
      if(!freed)
      {
        throw vgl_runtime_error("VulkanMemoryManager::free() failed!");
      }
#endif
    }

    void VulkanMemoryManager::reclaimMemory()
    {
      lock_guard<mutex> locker(managerLock);

      if(freeMode == FM_MANUAL)
      {
        for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
        {
          for(auto &alloc : allocations[i])
          {
            if(alloc->suballocationCount == 0)
            {
              vkFreeMemory(device, alloc->memory, nullptr);
              alloc->memory = VK_NULL_HANDLE;
            }
          }
        }
      }

      cleanupAllocations();
    }

    uint32_t VulkanMemoryManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
      for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
      {
        if((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
          return i;
        }
      }

      return NO_MEMORY_TYPE;
    }

    VkDeviceMemory VulkanMemoryManager::allocateDirect(uint32_t memoryType, VkDeviceSize requiredSize, bool imageOptimal)
    {
      lock_guard<mutex> locker(managerLock);

      auto ret = allocateDedicated(memoryType, requiredSize, 1, false, imageOptimal);

      if(ret.first)
      {
        auto &alloc = *allocations[memoryType].back();
        alloc.type = AT_UNKNOWN;

        return ret.first;
      }

      return VK_NULL_HANDLE;
    }

    pair<VkDeviceMemory, uint64_t> VulkanMemoryManager::allocateDedicated(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkDeviceSize blockSize, 
      size_t maxBlocks, bool allowSuballocation, bool imageOptimal)
    {
      lock_guard<mutex> locker(managerLock);

      uint32_t memoryType = findMemoryType(typeFilter, properties);

      if(memoryType != NO_MEMORY_TYPE)
        return allocateDedicated(memoryType, blockSize*maxBlocks + 8192, allowSuballocation, imageOptimal);

      return { VK_NULL_HANDLE, 0 };
    }

    bool VulkanMemoryManager::isAllocationCoherent(const Suballocation &suballocation)
    {
      return (memoryProperties.memoryTypes[suballocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationCoherent(uint64_t allocationId)
    {      
      auto &allocation = *allocationForId(allocationId);
      return (memoryProperties.memoryTypes[allocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationTypeCoherent(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
      uint32_t memoryType = findMemoryType(typeFilter, properties);
      return (memoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationHostCached(const Suballocation &suballocation)
    {
      return (memoryProperties.memoryTypes[suballocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationHostCached(uint64_t allocationId)
    {
      auto &allocation = *allocationForId(allocationId);
      return (memoryProperties.memoryTypes[allocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationHostVisible(const Suballocation &suballocation)
    {
      return (memoryProperties.memoryTypes[suballocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationHostVisible(uint64_t allocationId)
    {
      auto &allocation = *allocationForId(allocationId);
      return (memoryProperties.memoryTypes[allocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationDeviceLocal(const Suballocation &suballocation)
    {
      return (memoryProperties.memoryTypes[suballocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? true : false;
    }

    bool VulkanMemoryManager::isAllocationDeviceLocal(uint64_t allocationId)
    {
      auto &allocation = *allocationForId(allocationId);
      return (memoryProperties.memoryTypes[allocation.memoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? true : false;
    }

    VulkanMemoryManager::AllocationInfo VulkanMemoryManager::getAllocationInfo(Suballocation alloc)
    {
      AllocationInfo result;

      result.memory = alloc.memory;
      result.memoryType = alloc.memoryType;
      result.offset = alloc.offset;
      result.size = alloc.size*pageSize;
      result.allocationId = alloc.allocationId;

      return result;
    }

    pair<VkDeviceMemory, uint64_t> VulkanMemoryManager::allocateDedicated(uint32_t memoryType, VkDeviceSize requiredSize, bool allowSuballocation, bool imageOptimal)
    {
      pair<VkDeviceMemory, uint64_t> result = { VK_NULL_HANDLE, 0 };
      VkMemoryAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocInfo.allocationSize = requiredSize;
      allocInfo.memoryTypeIndex = memoryType;

      if(ai+1 > maxAllocations)
      {
        throw vgl_runtime_error("Over the allocations pool limit in VulkanMemoryManager");
      }

      if(vkAllocateMemory(device, &allocInfo, nullptr, &result.first) == VK_SUCCESS)
      {
        allocations[memoryType].push_back(&allocationsPool[ai++]);

        auto &alloc = *allocations[memoryType].back();
        alloc.memory = result.first;
        alloc.size = (uint32_t)fastCeil(requiredSize, pageSize);
        alloc.id = allocationIds++;
        alloc.type = AT_DEDICATED;
        alloc.memoryType = memoryType;
        alloc.suballocationCount = 0;
        alloc.regions = { { 0, alloc.size, allowSuballocation, subregionIds++ } };
        if(allowSuballocation)
          alloc.freeRegions = { { &alloc.regions.back(), alloc.regions.begin() } };
        else
          alloc.freeRegions = { };
        alloc.imageOptimal = imageOptimal;
        allocationsMap[memoryType][alloc.id] = allocations[memoryType].size()-1;

        result.second = alloc.id;
        allocatedBytes += requiredSize;
      }

      return result;
    }

    VkDeviceMemory VulkanMemoryManager::makeNewAllocation(uint32_t memoryType, AllocationType type, bool imageOptimal)
    {
      if(type == AT_UNKNOWN)
        throw vgl_runtime_error("Unknown allocation type requested in VulkanMemoryManager::makeNewAllocation()");

      VkDeviceSize size = allocSizeBoundaries[1][(int)type];
      VkDeviceMemory result = VK_NULL_HANDLE;
      VkMemoryAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocInfo.allocationSize = size;
      allocInfo.memoryTypeIndex = memoryType;

      if(ai+1 > maxAllocations)
      {
        throw vgl_runtime_error("Over the allocations pool limit in VulkanMemoryManager");
      }

      if(vkAllocateMemory(device, &allocInfo, nullptr, &result) != VK_SUCCESS)
      {
        return VK_NULL_HANDLE;
      }
      else
      {
        allocations[memoryType].push_back(&allocationsPool[ai++]);

        auto &alloc = *allocations[memoryType].back();
        alloc.memory = result;
        alloc.size = (uint32_t)fastCeil(size, pageSize);
        alloc.id = allocationIds++;
        alloc.type = type;
        alloc.memoryType = memoryType;
        alloc.suballocationCount = 0;
        alloc.regions = { { 0, alloc.size, true, subregionIds++ } };
        alloc.freeRegions = { { &alloc.regions.back(), alloc.regions.begin() } };
        alloc.imageOptimal = imageOptimal;
        allocationsMap[memoryType][alloc.id] = allocations[memoryType].size()-1;       

        allocatedBytes += size;
      }
      return result;
    }

    static inline VkDeviceSize align(VkDeviceSize size, VkDeviceSize alignment)
    {
      VkDeviceSize m = size % alignment;
      return m ? (size + (alignment - m)) : size;
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::findSuballocation(uint32_t memoryType, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment, AllocationType type, bool imageOptimal, uint64_t allocationId)
    {
      uint32_t requiredPageSize = (uint32_t)fastCeil(requiredSize, pageSize);
      Suballocation result = nullptr;
      
      auto searchAllocation = [=](Allocation *targetAllocation) {
        auto &allocation = *targetAllocation;
        Suballocation result = nullptr;

        for(auto srIt = allocation.freeRegions.begin(); srIt != allocation.freeRegions.end(); srIt++)
        {
          auto sr = srIt->first;
          VkDeviceSize withinPageOffset = 0;
          uint32_t alignedRequiredPageSz = requiredPageSize;

          if(requiredAlignment / pageSize > 1)
          {
            //handle required alignment that is larger than the page size
            VkDeviceSize fullOffset = align(sr->startPage*pageSize, requiredAlignment);
            withinPageOffset = fullOffset - (sr->startPage*pageSize);
            alignedRequiredPageSz = (uint32_t)fastCeil(requiredSize+withinPageOffset, pageSize);
          }

          if(sr->free && alignedRequiredPageSz <= sr->size)
          {
            result.memory = allocation.memory;
            result.offset = sr->startPage*pageSize + withinPageOffset;
            result.size = alignedRequiredPageSz;
            result.memoryType = memoryType;
            result.allocationId = allocation.id;

            auto region = divideSubregion(allocation, srIt->second, alignedRequiredPageSz);
            result.subregionIt = region;
            result.subregionId = region->id;
            allocation.suballocationCount++;
            
            return result;
          }
        }

        return result;
      };
      
      Allocation *targetAllocation = nullptr;
      for(auto &alloc : allocations[memoryType])
      {
        auto &allocation = *alloc;

        //find by type
        if(!allocationId)
        {
          if((lowMemoryFlags & (1ull<<memoryType)) == 0)
          {
            if(allocation.memory && allocation.type == type && allocation.imageOptimal == imageOptimal)
            {
              if(auto potentialResult = searchAllocation(alloc))
              {
                return potentialResult;
              }
            }
          }
          else
          {
            //widen our search to larger allocation types
            if((int)allocation.type <= (int)AT_LARGE && (int)allocation.type >= (int)type)
            {
              if(auto potentialResult = searchAllocation(alloc))
              {
                return potentialResult;
              }
            }
          }
        }
        //find by id
        else if(allocation.memory && allocation.id == allocationId && allocation.imageOptimal == imageOptimal)
        {
          if(auto potentialResult = searchAllocation(alloc))
          {
            return potentialResult;
          }
        }
      }

      /*ostringstream ostr;
      int total = 0;
      for(auto &alloc : allocations[memoryType])
      {
        for(auto sr : alloc.regions) if(!sr.free)
          total++;
      }
      ostr << "Total allocations: " << total << endl;
      OutputDebugStringA(ostr.str().c_str());*/

      return result;
    }

    list<VulkanMemoryManager::Subregion>::iterator VulkanMemoryManager::divideSubregion(Allocation &allocation, list<Subregion>::iterator region, uint32_t sizeInPages)
    {
      auto newRegionIter = allocation.regions.emplace(region, Subregion());
      auto newRegion = &(*newRegionIter);
      auto freeRegionKey = &(*region);

      newRegion->id = subregionIds++;
      newRegion->startPage = region->startPage;
      newRegion->size = sizeInPages;
      newRegion->free = false;
      
      if(region->size-sizeInPages == 0)
      {
        //right side is now empty, so we remove that region entirely
        allocation.freeRegions.erase(freeRegionKey);
        allocation.regions.erase(region);
      }
      else
      {
        //we've modified the old region's size, which affects its position within the freeRegions map tree
        //so we actually have to remove & re-insert it here now
        allocation.freeRegions.erase(freeRegionKey);
        region->size -= sizeInPages;
        region->startPage = region->startPage+sizeInPages;
        allocation.freeRegions.insert({ freeRegionKey, region });
        
        VGL_CORE_PERF_WARNING_DEBUG(allocation.regions.size() > 1024, "Vulkan memory manager moderate heap fragmentation detected!")
      }

      return newRegionIter;
    }

    void VulkanMemoryManager::mergeSubregions(Allocation &allocation, list<Subregion>::iterator region1, std::list<Subregion>::iterator region2)
    {
      assert(region1->free && region2->free);
      region2->startPage -= region1->size;
      region2->size += region1->size;
      allocation.regions.erase(region1);
    }

    VulkanMemoryManager::Allocation *VulkanMemoryManager::allocationForId(uint64_t allocationId)
    {
      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        auto ait = allocationsMap[i].find(allocationId);

        if(ait != allocationsMap[i].end())
          return allocations[i][ait->second];
      }

      return nullptr;
    }

    void VulkanMemoryManager::cleanupAllocations()
    {
      /*** As it currently stands, this is the most expensive operation you can call within the entire memory manager ***/

      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        auto &v = allocations[i];
        v.erase(remove_if(v.begin(), v.end(), [](const Allocation *a) { return (a->memory == VK_NULL_HANDLE); }), v.end());

        allocationsMap[i].clear();
        for(size_t j = 0; j < allocations[i].size(); j++)
          allocationsMap[i][allocations[i][j]->id] = j;
      }

      int newAi = 0;
      auto newAllocationsPool = new Allocation[maxAllocations];
      for(int i = 0; i < ai; i++)
      {
        if(allocationsPool[i].memory)
        {
          bool found = false;

          newAllocationsPool[newAi] = allocationsPool[i];

          //need to fix references within free regions now
          //Note:  this operation could be quite slow for certain situations O(n^2)
          newAllocationsPool[newAi].freeRegions.clear();
          for(auto &fr : allocationsPool[i].freeRegions)
          {
            auto &regions = newAllocationsPool[newAi].regions;

            found = false;
            for(auto srIt = regions.begin(); srIt != regions.end(); srIt++)
            {
              if(srIt->id == fr.first->id)
              {
                auto sr = &(*srIt);

                newAllocationsPool[newAi].freeRegions[sr] = srIt;
                found = true;
                break;
              }
            }

            if(DebugBuild() && !found)
              throw vgl_runtime_error("Internal error in VulkanMemoryManager::cleanupAllocations()");
          }

          //and now we need to find and replace all the pointers in the allocations arrays
          //unfortunately, we don't store memory type index inside allocations struct
          found = false;
          for(uint32_t mi = 0; mi < VK_MAX_MEMORY_TYPES; mi++)
          {
            for(auto &allocPtr : allocations[mi])
            {
              if(allocPtr->id == newAllocationsPool[newAi].id)
              {
                allocPtr = &newAllocationsPool[newAi];
                found = true;
                break;
              }
            }            
          }

          if(DebugBuild() && !found)
            throw vgl_runtime_error("Internal error in VulkanMemoryManager::cleanupAllocations()");

          newAi++;
        }
      }

      delete []allocationsPool;
      allocationsPool = newAllocationsPool;
      ai = newAi;

      //any freed subregions less than this id won't be able to use their list iterators
      //(this is why you should avoid calling reclaimMemory often)
      invalidatedSubregionIds = subregionIds;
    }

    //We use this as an opportunity to keep our free regions list sorted by increasing size
    bool VulkanMemoryManager::FreeRegionComparator::operator()(const Subregion *a, const Subregion *b) const
    {
      return (a->size == b->size) ? (a < b) : (a->size < b->size);
    }

    void VulkanMemoryManager::dumpAllocationsInfo()
    {
      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        vout << "--- Memory type: " << i << "---" << endl;
        for(auto &allocation : allocations[i]) if(allocation->memory)
        {
          vout << "-- allocation type: " << allocation->type << ", image optimal: " << allocation->imageOptimal << endl;
          vout << "regions [ ";
          for(auto &region : allocation->regions)
          {
            vout << "\t( start: " << region.startPage << ", size: " << region.size << ", free: " << region.free << ")" << endl;
          }          
          vout << "]" << endl;
        }
      }
    }
#endif
  }
}
