#include "pch.h"
#include <cassert>
#include <algorithm>
#include "VulkanMemoryManager.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    static VulkanMemoryManager *managerInstance = nullptr;
    VulkanMemoryManager::AllocationStrategy VulkanMemoryManager::allocationStrategy = VulkanMemoryManager::AS_MOBILE_CONVERVATIVE;
    VulkanMemoryManager::FreeMode VulkanMemoryManager::freeMode = VulkanMemoryManager::FM_MANUAL;
    VkDeviceSize VulkanMemoryManager::allocSizeBoundaries[2][3];

    static inline uint64_t fastCeil(uint64_t x, uint64_t y) { return (1 + ((x - 1) / y)); }

    VulkanMemoryManager &VulkanMemoryManager::manager()
    {
      return *managerInstance;
    }

    VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physicalDevice, VkDevice device)
      : physicalDevice(physicalDevice), device(device)
    {
      assert(!managerInstance);
      managerInstance = this;

      switch(allocationStrategy)
      {
        case AS_MOBILE_CONVERVATIVE:
          allocSizeBoundaries[0][0] = (512<<10);  //512k
          allocSizeBoundaries[0][1] = (5<<20);    //5 mb
          allocSizeBoundaries[0][2] = (100<<20);  //100 mb

          allocSizeBoundaries[1][0] = (1<<20);    //1 mb
          allocSizeBoundaries[1][1] = (25<<20);   //25 mb
          allocSizeBoundaries[1][2] = (100<<20);  //100 mb
        break;
        case AS_DESKTOP:
          allocSizeBoundaries[0][0] = (1<<10);    //1 mb
          allocSizeBoundaries[0][1] = (100<<20);  //25 mb
          allocSizeBoundaries[0][2] = (500<<20);  //500 mb

          allocSizeBoundaries[1][0] = (10<<20);   //10 mb
          allocSizeBoundaries[1][1] = (100<<20);  //100 mb
          allocSizeBoundaries[1][2] = (250<<20);  //500 mb
        break;
      }

      vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        for(auto &allocation : allocations[i]) if(allocation.memory)
          vkFreeMemory(device, allocation.memory, nullptr);
      }
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::allocate(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment)
    {     
      lock_guard<mutex> locker(managerLock);
      if(requiredSize == 0)
        return {};

      AllocationType allocType = AT_SMALL;

      if(requiredSize > allocSizeBoundaries[0][0])
        allocType = AT_MED;
      else if(requiredSize > allocSizeBoundaries[0][1])
        allocType = AT_LARGE;
      else if(requiredSize > allocSizeBoundaries[0][2])
        return {};

      uint32_t memoryType = findMemoryType(typeFilter, properties);
      Suballocation suballoc = findSuballocation(memoryType, requiredSize, requiredAlignment, allocType);

      if(!suballoc)
      {
        //have to create a new allocation
        makeNewAllocation(memoryType, allocType);
        suballoc = findSuballocation(memoryType, requiredSize, requiredAlignment, allocType);

#ifdef DEBUG
        //this shouldn't be possible
        if(requiredAlignment > 0 && suballoc.offset > 0)
          throw runtime_error("VulkanMemoryManager::allocate alignment ate shit");
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

      if(ai != allocMap.end())
      {
        auto &allocation = allocations[suballocation.memoryType][ai->second];

        if(allocation.suballocationCount > 0)
        {        
          auto srIt = suballocation.subregionIt;
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

            if(allocation.suballocationCount == 0 && freeMode == FM_SYNCHRONOUS)
            {
              //this entire allocation is freed now
              vkFreeMemory(device, allocation.memory, nullptr);
              allocation.memory = nullptr;
            }
          }
        }          
      }

#ifdef DEBUG
      if(!freed)
      {
        throw runtime_error("VulkanMemoryManager::free() failed!");
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
            if(alloc.suballocationCount == 0 && freeMode == FM_SYNCHRONOUS)
            {
              vkFreeMemory(device, alloc.memory, nullptr);
              alloc.memory = nullptr;
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

      throw runtime_error("VulkanMemoryManager::findMemoryType failed to find suitable memory type!");
    }

    VkDeviceMemory VulkanMemoryManager::allocateDirect(uint32_t memoryType, VkDeviceSize requiredSize)
    {
      lock_guard<mutex> locker(managerLock);
      VkDeviceMemory result = nullptr;
      VkMemoryAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocInfo.allocationSize = requiredSize;
      allocInfo.memoryTypeIndex = memoryType;

      if(vkAllocateMemory(device, &allocInfo, nullptr, &result) == VK_SUCCESS)
      {
        Allocation empty = {};
        allocations[memoryType].emplace_back(empty);

        auto &alloc = allocations[memoryType].back();
        alloc.memory = result;
        alloc.size = (uint32_t)fastCeil(requiredSize, pageSize);
        alloc.id = allocationIds++;
        alloc.type = AT_UNKNOWN;
        alloc.regions = { { 0, alloc.size, true, subregionIds++ } };
        alloc.freeRegions = { { &alloc.regions.back(), alloc.regions.begin() } };
        allocationsMap[memoryType][alloc.id] = allocations[memoryType].size()-1;
      }
      else
      {
        result = nullptr;
      }      

      return result;
    }

    VkDeviceMemory VulkanMemoryManager::makeNewAllocation(uint32_t memoryType, AllocationType type)
    {
      if(type == AT_UNKNOWN)
        throw runtime_error("Unknown allocation type requested in VulkanMemoryManager::makeNewAllocation()");

      VkDeviceSize size = allocSizeBoundaries[1][(int)type];
      VkDeviceMemory result = nullptr;
      VkMemoryAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocInfo.allocationSize = size;
      allocInfo.memoryTypeIndex = memoryType;

      if(vkAllocateMemory(device, &allocInfo, nullptr, &result) != VK_SUCCESS)
      {
        return nullptr;
      }
      else
      {
        Allocation empty = {};
        allocations[memoryType].emplace_back(empty);

        auto &alloc = allocations[memoryType].back();
        alloc.memory = result;
        alloc.size = (uint32_t)fastCeil(size, pageSize);
        alloc.id = allocationIds++;
        alloc.type = type;
        alloc.regions = { { 0, alloc.size, true, subregionIds++ } };
        alloc.freeRegions = { { &alloc.regions.back(), alloc.regions.begin() } };
        allocationsMap[memoryType][alloc.id] = allocations[memoryType].size()-1;        
      }

      return result;
    }

    VulkanMemoryManager::Suballocation VulkanMemoryManager::findSuballocation(uint32_t memoryType, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment, AllocationType type)
    {
      Suballocation result = nullptr;
      uint32_t requiredPageSize = (uint32_t)fastCeil(requiredSize, pageSize);

      //it'd be extremely unlikely for 4096 to not work for a requested alignment
      if(requiredAlignment / pageSize != 0)
      {
        throw runtime_error("VulkanMemoryManager::findSuballocation() failed to obtain correct alignment for suballocation");
      }
      
      for(auto &allocation : allocations[memoryType]) if(allocation.memory && allocation.type == type)
      {
        for(auto srIt = allocation.freeRegions.begin(); srIt != allocation.freeRegions.end(); srIt++)
        {
          auto sr = srIt->first;

          if(sr->free && requiredPageSize <= sr->size)
          {
            result.memory = allocation.memory;
            result.offset = sr->startPage*pageSize;
            result.size = requiredPageSize;
            result.memoryType = memoryType;
            result.allocationId = allocation.id;

            auto region = divideSubregion(allocation, srIt->second, requiredPageSize);
            result.subregionIt = region;
            result.subregionId = region->id;
            allocation.suballocationCount++;

            break;
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

      newRegion->id = subregionIds++;
      newRegion->startPage = region->startPage;
      newRegion->size = sizeInPages;
      newRegion->free = false;
      
      region->size -= sizeInPages;
      region->startPage = region->startPage+sizeInPages;
      if(region->size == 0)
      {
        //right side is empty, this isn't even a valid region now
        //this allocation is officially filled up
        allocation.freeRegions.erase(&(*region));
        allocation.regions.erase(region);
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

    void VulkanMemoryManager::cleanupAllocations()
    {
      for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
      {
        auto &v = allocations[i];
        v.erase(remove_if(v.begin(), v.end(), [](const Allocation &a) { return (a.memory == nullptr); }), v.end());

        allocationsMap[i].clear();
        for(size_t j = 0; j < allocations[i].size(); j++)
          allocationsMap[i][allocations[i][j].id] = j;
      }
    }
  }
}