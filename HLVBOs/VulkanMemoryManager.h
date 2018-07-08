#pragma once

#include <vector>
#include <map>
#include <list>
#include <mutex>
#include "vulkan.h"

namespace vgl
{
  namespace core
  {
    ///This class will get more advanced as time passes
    class VulkanMemoryManager
    {
    public:
      enum AllocationStrategy
      {
        AS_MOBILE_CONVERVATIVE,
        AS_DESKTOP
      };

      enum FreeMode
      {
        FM_MANUAL,
        FM_SYNCHRONOUS
      };

      ///Unlike other classes with static accessors, one instance of this class must already
      ///Exist before calling manager().  manager() will not create one for you
      static VulkanMemoryManager &manager();

      VulkanMemoryManager(VkPhysicalDevice physicalDevice, VkDevice device);
      ~VulkanMemoryManager();

    protected:
      enum AllocationType
      {
        AT_SMALL = 0, AT_MED, AT_LARGE, AT_UNKNOWN
      };

      struct Subregion
      {
        uint32_t startPage, size;
        bool free;
        uint64_t id;

        //Subregion *next = nullptr, *prev = nullptr;
        //static Subregion *create();
        //void destroy();
      };

      struct Allocation
      {
        VkDeviceMemory memory;
        uint32_t size; //in pages
        uint32_t suballocationCount;
        uint64_t id;
        AllocationType type;
        std::list<Subregion> regions;
        std::map<Subregion *, std::list<Subregion>::iterator> freeRegions;
      };

    public:
      struct Suballocation
      {
      public:
        VkDeviceMemory memory;
        VkDeviceSize offset;

        uint32_t size; //size in pages
        uint32_t memoryType;
        uint64_t allocationId, subregionId;
        std::list<Subregion>::iterator subregionIt;
        
        Suballocation() = default;
        Suballocation(nullptr_t null) : memory(VK_NULL_HANDLE) {}
        void operator =(nullptr_t null) { memory = VK_NULL_HANDLE; }
        operator bool() const { return (memory != nullptr); }
        bool operator !() const { return (memory == nullptr); }
      };

      Suballocation allocate(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment=0);
      void free(const Suballocation &suballocation);

      ///If free mode is set to AT_MANUAL, you'll need to call this before any actual Vulkan allocations are freed
      void reclaimMemory();

      ///When using this function, the caller is entirely responsible for the returned memory object
      VkDeviceMemory allocateDirect(uint32_t memoryType, VkDeviceSize requiredSize);   

    protected:
      static const int pageSize = 4096;

      static AllocationStrategy allocationStrategy;
      static FreeMode freeMode;
      static VkDeviceSize allocSizeBoundaries[2][3];

      //calls to all public methods will be synchronized across threads
      std::mutex managerLock;

      uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
      VkDeviceMemory makeNewAllocation(uint32_t memoryType, AllocationType type);
      Suballocation findSuballocation(uint32_t memoryType, VkDeviceSize requiredSize, VkDeviceSize requiredAlignment, AllocationType type);
      void cleanupAllocations();

      VkPhysicalDevice physicalDevice;
      VkPhysicalDeviceMemoryProperties memoryProperties;

      std::list<Subregion>::iterator divideSubregion(Allocation &allocation, std::list<Subregion>::iterator region, uint32_t sizeInPages);
      void mergeSubregions(Allocation &allocation, std::list<Subregion>::iterator region1, std::list<Subregion>::iterator region2);

      VkDevice device;
      std::vector<Allocation> allocations[VK_MAX_MEMORY_TYPES];
      std::map<uint64_t, uint64_t> allocationsMap[VK_MAX_MEMORY_TYPES];
      //std::vector<Subregion> subregionPools[VK_MAX_MEMORY_TYPES];
      uint64_t allocationIds = 1, subregionIds = 1;
    };
  }
}