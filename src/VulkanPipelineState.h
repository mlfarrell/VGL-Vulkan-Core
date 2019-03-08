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
    struct VulkanPipelineState
    {
      //This class must have a linear memory layout and thus...
      //All pointers in these vulkan struct members are to be set to nullptr
      VkPipelineViewportStateCreateInfo viewport;
      VkRect2D scissor0;
      VkViewport viewport0;

      VkPipelineInputAssemblyStateCreateInfo inputAssembly;
      VkPipelineRasterizationStateCreateInfo rasterizer;

      VkPipelineColorBlendAttachmentState blendAttachment0;
      VkPipelineColorBlendStateCreateInfo blend;

      VkPipelineDepthStencilStateCreateInfo depthStencil;
      
      VkPipelineMultisampleStateCreateInfo multiSample;
      VkSampleMask multiSampleMask;
      bool multiSampleMaskEnable;

      uint32_t numVertexAttributes;
      uint32_t numVertexBindings;
      VkVertexInputAttributeDescription vertexInputAttributes[16];
      VkVertexInputBindingDescription vertexInputBindings[16];

      uint32_t vaoIdentifier, fboIdentifier;
    };
  }
}
