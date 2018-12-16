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
#include <algorithm>
#include "VulkanVertexArray.h"
#include "VulkanMemoryManager.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "System.h"
#include "StateMachine.h"
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanVertexArray::VulkanVertexArray(VkDevice device)
      : device(device)
    {
      memset(enabledAttributeStates, 0, sizeof(bool)*MaxBindings);
    }

    VulkanVertexArray::~VulkanVertexArray()
    {
    }

    void VulkanVertexArray::setAttribute(int location, VulkanBufferGroup *bufferGroup, int bufferIndex, VkFormat format, int offset, int stride, bool instanced)
    {
      attributeDescriptions[location].location = location;
      attributeDescriptions[location].format = format;
      attributeDescriptions[location].offset = offset;
      bindingInfo[location].stride = stride;
      bindingInfo[location].instanced = instanced;
      bindingInfo[location].buffer = bufferGroup;
      bindingInfo[location].bufferIndex = bufferIndex;
      numDefinedAttributes = max(numDefinedAttributes, location+1);

      bindingsDirty = true;
    }

    void VulkanVertexArray::enableAttribute(int location, bool enabled)
    {
      enabledAttributeStates[location] = enabled;
      bindingsDirty = true;
    }

    void VulkanVertexArray::bind(VkCommandBuffer commandBuffer)
    {
      VkBuffer vkBuffers[MaxBindings];
      VkDeviceSize offsets[MaxBindings] = { 0 };

#ifndef VGL_VULKAN_CORE_STANDALONE
      auto &csm = vgl::StateMachine::machine().getCoreStateMachine();

      if(bindingsDirty)
      {
        updateBindings();        
      }
      csm->setInputLayout(this);
      if(!commandBuffer)
        commandBuffer = csm->getRenderingCommandBuffer();
#else
      if(bindingsDirty)
      {
        updateBindings();
      }
#endif

      for(int i = 0; i < numBindings; i++)
      {
        vkBuffers[i] = bufferBindings[i]->get(bufferBindingIndices[i]);
      }
      vkCmdBindVertexBuffers(commandBuffer, 0, numBindings, vkBuffers, offsets);
    }

    int VulkanVertexArray::getNumBindings() 
    {
      if(bindingsDirty)
        updateBindings();

      return numBindings;
    }

    const VkVertexInputBindingDescription *VulkanVertexArray::getBindings() 
    {
      if(bindingsDirty)
        updateBindings();

      return bindingDescriptions;
    }

    int VulkanVertexArray::getNumAttributes()
    {
      if(bindingsDirty)
        updateBindings();

      return numAttributes;
    }

    const VkVertexInputAttributeDescription *VulkanVertexArray::getAttributes()
    {
      if(bindingsDirty)
        updateBindings();

      return enabledAttributeDescriptions;
    }

    void VulkanVertexArray::updateBindings()
    {
      bindingsDirty = false;
      numBindings = 0;
      numAttributes = 0;

      for(int i = 0; i < numDefinedAttributes; i++)
      {
        int location = attributeDescriptions[i].location;

        if(enabledAttributeStates[i])
        {
          VkVertexInputBindingDescription bindingDescription = {};
          bindingDescription.binding = numBindings;
          bindingDescription.stride = bindingInfo[location].stride;
          bindingDescription.inputRate = (!bindingInfo[location].instanced) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
          bindingDescriptions[numBindings] = bindingDescription;
          attributeDescriptions[i].binding = numBindings;

          bufferBindings[numBindings] = bindingInfo[location].buffer;
          bufferBindingIndices[numBindings] = bindingInfo[location].bufferIndex;

          enabledAttributeDescriptions[numBindings] = attributeDescriptions[i];
          enabledAttributeDescriptions[numBindings].binding = numBindings;

          numBindings++;
          numAttributes++;
        }
      }
    }
  }
}