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
#include <vector>
#include "VulkanDescriptorSetLayout.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, Binding vertexUboBinding, Binding fragmentUboBinding, Binding fragmentCombinedSamplerBinding, bool dynamicUbos)
      : device(device)
    {
      VkDescriptorSetLayoutCreateInfo createInfo = {};
      vector<VkDescriptorSetLayoutBinding> bindings;

      bindings.reserve(vertexUboBinding.bindingCount+fragmentUboBinding.bindingCount+fragmentCombinedSamplerBinding.bindingCount);
           
      for(int i = 0; i < vertexUboBinding.bindingCount; i++)
      {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = vertexUboBinding.binding+i;
        uboLayoutBinding.descriptorType = (!dynamicUbos) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; //VkTodo: this is stupid
        bindings.push_back(uboLayoutBinding);
      }

      for(int i = 0; i < fragmentUboBinding.bindingCount; i++)
      {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = fragmentUboBinding.binding+i;
        uboLayoutBinding.descriptorType = (!dynamicUbos) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(uboLayoutBinding);
      }

      for(int i = 0; i < fragmentCombinedSamplerBinding.bindingCount; i++)
      {
        VkDescriptorSetLayoutBinding layoutBinding = {};
        layoutBinding.binding = fragmentCombinedSamplerBinding.binding+i;
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(layoutBinding);
      }

      createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      createInfo.bindingCount = (int)bindings.size();
      createInfo.pBindings = bindings.data();
      create(createInfo);
    }

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo &createInfo)
      : device(device)
    {
      create(createInfo);
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
    {
      vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }

    void VulkanDescriptorSetLayout::create(const VkDescriptorSetLayoutCreateInfo &createInfo)
    {
      if(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &layout) != VK_SUCCESS)
      {
        throw vgl_runtime_error("failed to create descriptor set layout!");
      }
    }
  }
}