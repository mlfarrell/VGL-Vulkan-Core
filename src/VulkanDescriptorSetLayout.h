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
    class VulkanDescriptorSetLayout
    {
    public:
      ///Simple convenient way to specify an most common type of descriptor set layout
      struct Binding
      {
        int binding;
        int bindingCount;
      };
      VulkanDescriptorSetLayout(VkDevice device, Binding vertexUboBinding, Binding fragmentUboBinding, Binding fragmentCombinedSamplerBinding, bool dynamicUbos=false);

      ///Explicit way to create descriptor set layout
      VulkanDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo &createInfo);

      ~VulkanDescriptorSetLayout();

      inline VkDescriptorSetLayout get() { return layout; }

    protected:
      VkDevice device;
      VkDescriptorSetLayout layout;

      void create(const VkDescriptorSetLayoutCreateInfo &createInfo);
    };
  }
}