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
#include <stdexcept>
#include "VulkanPipeline.h"
#include "VulkanPipelineState.h"
#include "VulkanShaderProgram.h"
#include "VulkanVertexArray.h"
#include "VulkanDescriptorSetLayout.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanPipeline::VulkanPipeline(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass,
      VulkanShaderProgram *shader, VulkanVertexArray *vertexArray, VulkanDescriptorSetLayout **descriptorSetLayouts, int numSetLayouts,
      VkPipelineCache pipelineCache)
      : device(device)
    {
      VkDescriptorSetLayout layouts[16];
      for(int i = 0; i < numSetLayouts; i++)
        layouts[i] = descriptorSetLayouts[i]->get();
      VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
      pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipelineLayoutInfo.setLayoutCount = numSetLayouts;
      pipelineLayoutInfo.pSetLayouts = layouts;
      pipelineLayoutInfo.pushConstantRangeCount = 0;
      pipelineLayoutInfo.pPushConstantRanges = nullptr;

      if(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw vgl_runtime_error("failed to create pipeline layout!");

      create(device, state, renderPass, shader, vertexArray, pipelineLayout, pipelineCache);
    }

    VulkanPipeline::VulkanPipeline(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass, VulkanShaderProgram *shader, 
      VulkanVertexArray *vertexArray, VkPipelineLayout layout, VkPipelineCache pipelineCache)
      : device(device)
    {
      create(device, state, renderPass, shader, vertexArray, layout, pipelineCache);
    }

    void VulkanPipeline::create(VkDevice device, const VulkanPipelineState *state, VkRenderPass renderPass, VulkanShaderProgram *shader, 
      VulkanVertexArray *vertexArray, VkPipelineLayout layout, VkPipelineCache pipelineCache)
    {
      uint32_t numShaderStages = 2;
      VkPipelineShaderStageCreateInfo shaderStages[3] = {};
      VkPipelineShaderStageCreateInfo &vertShaderStageInfo = shaderStages[0], &fragShaderStageInfo = shaderStages[1], &geomShaderStageInfo = shaderStages[2];

      vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
      vertShaderStageInfo.module = shader->getVertexShader();
      vertShaderStageInfo.pName = "main";

      if(auto geomShader = shader->getGeometryShader())
      {
        geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        geomShaderStageInfo.module = geomShader;
        geomShaderStageInfo.pName = "main";
        numShaderStages++;
      }

      fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      fragShaderStageInfo.module = shader->getFragmentShader();
      fragShaderStageInfo.pName = "main";

      VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
      vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
      if(vertexArray)
      {
        vertexInputInfo.vertexBindingDescriptionCount = vertexArray->getNumBindings();
        vertexInputInfo.vertexAttributeDescriptionCount = vertexArray->getNumAttributes();
        vertexInputInfo.pVertexBindingDescriptions = vertexArray->getBindings();
        vertexInputInfo.pVertexAttributeDescriptions = vertexArray->getAttributes();
      }
      else
      {
        vertexInputInfo.vertexBindingDescriptionCount = state->numVertexBindings;
        vertexInputInfo.pVertexBindingDescriptions = state->vertexInputBindings;
        vertexInputInfo.vertexAttributeDescriptionCount = state->numVertexAttributes;
        vertexInputInfo.pVertexAttributeDescriptions = state->vertexInputAttributes;
      }

      VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
      inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
      inputAssembly.topology = state->inputAssembly.topology;
      inputAssembly.primitiveRestartEnable = state->inputAssembly.primitiveRestartEnable;
      inputAssembly.flags = state->inputAssembly.flags;

      VkPipelineViewportStateCreateInfo viewportState = {};
      memcpy(&viewportState, &state->viewport, sizeof(state->viewport));
      viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
      viewportState.viewportCount = 1;
      viewportState.pViewports = &state->viewport0;
      viewportState.scissorCount = 1;
      viewportState.pScissors = &state->scissor0;

      VkPipelineRasterizationStateCreateInfo rasterizer = {};
      memcpy(&rasterizer, &state->rasterizer, sizeof(state->rasterizer));
      rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

      VkPipelineMultisampleStateCreateInfo multisampling = {};
      memcpy(&multisampling, &state->multiSample, sizeof(state->multiSample));
      multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
      multisampling.pSampleMask = (state->extraStateFlags & VPSF_MULTI_SAMPLE_MASK_ENABLE) ? &state->multiSampleMask : nullptr;

      VkPipelineColorBlendStateCreateInfo colorBlending = {};
      VkPipelineColorBlendAttachmentState blendAttachmentStates[16];
      memcpy(&colorBlending, &state->blend, sizeof(state->blend));
      colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
      colorBlending.pAttachments = &state->blendAttachment0;
      if(state->extraStateFlags & VPSF_REPEAT_BLEND_ATTACHMENT0 && colorBlending.attachmentCount > 1)
      {
        for(int i = 0; i < colorBlending.attachmentCount; i++)
          memcpy(&blendAttachmentStates[i], &state->blendAttachment0, sizeof(VkPipelineColorBlendAttachmentState));
        
        colorBlending.pAttachments = blendAttachmentStates;
      }

      VkPipelineDepthStencilStateCreateInfo depthStencil = {};
      memcpy(&depthStencil, &state->depthStencil, sizeof(state->depthStencil));
      depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

      VkGraphicsPipelineCreateInfo pipelineInfo = {};
      pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      pipelineInfo.stageCount = numShaderStages;
      pipelineInfo.pStages = shaderStages;
      pipelineInfo.pVertexInputState = &vertexInputInfo;
      pipelineInfo.pInputAssemblyState = &inputAssembly;
      pipelineInfo.pViewportState = &viewportState;
      pipelineInfo.pRasterizationState = &rasterizer;
      pipelineInfo.pMultisampleState = &multisampling;
      pipelineInfo.pDepthStencilState = &depthStencil;
      pipelineInfo.pColorBlendState = &colorBlending;
      pipelineInfo.pDynamicState = nullptr;
      pipelineInfo.layout = layout;
      pipelineInfo.renderPass = renderPass;
      pipelineInfo.subpass = 0;

      pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
      pipelineInfo.basePipelineIndex = -1;
      pipelineInfo.flags = 0;

      if(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan Graphics Pipeline!");
    }

    VulkanPipeline::~VulkanPipeline()
    {
      vkDestroyPipeline(device, pipeline, nullptr);
      if(pipelineLayout)
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
 }
}
