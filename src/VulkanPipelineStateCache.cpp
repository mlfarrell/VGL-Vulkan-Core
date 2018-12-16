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
#include "VulkanPipelineStateCache.h"
#include "VulkanPipeline.h"
#include "VulkanPipelineState.h"
#include "VulkanVertexArray.h"
#include "VulkanFrameBuffer.h"
#include "VulkanShaderProgram.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    //-----------------------------------------------------------------------------
    // MurmurHash2, 64-bit versions, by Austin Appleby

    // The same caveats as 32-bit MurmurHash2 apply here - beware of alignment
    // and endian-ness issues if used across multiple platforms.

    //typedef unsigned __int64 uint64_t;

    // 64-bit hash for 64-bit platforms

    static uint64_t MurmurHash64A(const void *key, int len, unsigned int seed)
    {
      const uint64_t m = 0xc6a4a7935bd1e995;
      const int r = 47;

      uint64_t h = seed ^ (len * m);

      const uint64_t *data = (const uint64_t *)key;
      const uint64_t *end = data + (len / 8);

      while(data != end)
      {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
      }

      const unsigned char *data2 = (const unsigned char *)data;

      switch(len & 7)
      {
      case 7: h ^= uint64_t(data2[6]) << 48;
      case 6: h ^= uint64_t(data2[5]) << 40;
      case 5: h ^= uint64_t(data2[4]) << 32;
      case 4: h ^= uint64_t(data2[3]) << 24;
      case 3: h ^= uint64_t(data2[2]) << 16;
      case 2: h ^= uint64_t(data2[1]) << 8;
      case 1: h ^= uint64_t(data2[0]);
        h *= m;
      };

      h ^= h >> r;
      h *= m;
      h ^= h >> r;

      return h;
    }

    VulkanPipelineStateCache::VulkanPipelineStateCache(VkDevice device, VulkanShaderProgram *owner) 
      : device(device), owner(owner)
    {
      vulkanSystemCache = VulkanInstance::currentInstance().getPipelineCache();
    }

    VulkanPipelineStateCache::~VulkanPipelineStateCache()
    {
      for(const auto &cachedPSO : cachedPSOs)
        delete cachedPSO.second.pipeline;
    }

    VulkanPipeline *VulkanPipelineStateCache::getCachedPipeline(const VulkanPipelineState &state, VulkanFrameBuffer *fbo)
    {
      size_t sz = sizeof(VulkanPipelineState);
      uint64_t hashKey = MurmurHash64A(&state, (int)sz, (unsigned int)sz);
      VulkanPipeline *pipelineState = nullptr;

      auto er = cachedPSOs.equal_range(hashKey);
      int count = 0;
      static bool warned = false;
      for(auto it = er.first; it != er.second; it++)
      {
        if(count)
        {
          if(!warned)
          {
            cerr << "Hash collision on getCachedPSO.  This is generally very bad for performance!" << std::endl;
            warned = true;
          }

          if(count == 1)
          {
            //go back and rule out first that we skipped
            if(pipelineStatesEqual(state, er.first->second.desc))
            {
              pipelineState = er.first->second.pipeline;

              break;
            }
          }

          if(pipelineStatesEqual(state, it->second.desc))
          {
            pipelineState = it->second.pipeline;
            break;
          }
        }
        count++;
      }

      if(count == 1)
        pipelineState = er.first->second.pipeline;

      if(!pipelineState)
      {
        /*if(DebugBuild())
        {
          vout << "Creating new pipeline state object in cache for hash value: " << hashKey << std::endl;
        }*/

        pipelineState = new VulkanPipeline(device, &state, fbo->getRenderPass(), owner, nullptr, owner->getPipelineLayout(), vulkanSystemCache);
        cachedPSOs.insert({ hashKey, { state, pipelineState } });
      }

      return pipelineState;
    }
  }
}