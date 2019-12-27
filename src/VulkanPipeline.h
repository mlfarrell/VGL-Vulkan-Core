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

#include "vulkan.h"

namespace vgl
{
  namespace core
  {
    class VulkanShaderProgram;
    class VulkanVertexArray;
    class VulkanDescriptorSetLayout;
    struct VulkanPipelineState;

    class VulkanPipeline
    {
    public:
      VulkanPipeline(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass, 
        VulkanShaderProgram *shader, VulkanVertexArray *vertexArray = nullptr, VulkanDescriptorSetLayout **descriptorSetLayouts = nullptr,
        int numSetLayouts = 0, VkPipelineCache pipelineCache = VK_NULL_HANDLE);
      VulkanPipeline(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass, VulkanShaderProgram *shader, 
        VulkanVertexArray *vertexArray, VkPipelineLayout layout = VK_NULL_HANDLE, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

      ~VulkanPipeline();

      inline VkPipeline get() { return pipeline; }
      inline VkPipelineLayout getLayout() { return pipelineLayout; }
      
    protected:
      void create(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass,
        VulkanShaderProgram *shader, VulkanVertexArray *vertexArray, VkPipelineLayout layout, VkPipelineCache pipelineCache);
      void createCompute(VkDevice device, VulkanShaderProgram *shader, VkPipelineLayout layout, VkPipelineCache pipelineCache);

      VkDevice device;
      VkPipeline pipeline = VK_NULL_HANDLE;
      VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

    typedef VulkanPipeline Pipeline;
  }
}
