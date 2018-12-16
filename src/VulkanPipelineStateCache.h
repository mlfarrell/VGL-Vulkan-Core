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

#include <unordered_map>
#include "vulkan.h"
#include "VulkanPipelineState.h"

#ifndef VGLINLINE
#define VGLINLINE inline
#endif

namespace vgl
{
  namespace core
  {    
    class VulkanPipeline;
    class VulkanVertexArray;
    class VulkanFrameBuffer;
    class VulkanShaderProgram;

    ///The idea behind this class is to provide efficient tracking for any GL-like state changes and
    ///to route to the specific cached VulkanPipeline that fits (or create a new one if needed)
    class VulkanPipelineStateCache
    {
    public:
      VulkanPipelineStateCache(VkDevice device, VulkanShaderProgram *owner);
      ~VulkanPipelineStateCache();

      VulkanPipeline *getCachedPipeline(const VulkanPipelineState &state, VulkanFrameBuffer *fbo);

      VGLINLINE bool pipelineStatesEqual(const VulkanPipelineState &lhs, const VulkanPipelineState &rhs)
      {
        //this is NOT fast, but will only be ran in the event of a hash collision
        return memcmp(&lhs, &rhs, sizeof(VulkanPipelineState)) == 0;
      }

    protected:
      VkDevice device;
      VulkanShaderProgram *owner;
      VkPipelineCache vulkanSystemCache;

      class PipelineState
      {
      public:
        VulkanPipelineState desc;
        VulkanPipeline *pipeline;
      };

      std::unordered_multimap<uint64_t, PipelineState> cachedPSOs;
    };
  }
}
