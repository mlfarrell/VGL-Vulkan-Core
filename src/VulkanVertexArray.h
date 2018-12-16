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

#include <vector>
#include "vulkan.h"
#include "VulkanBufferGroup.h"
#include "SequentialIdentifier.h"

namespace vgl
{
  namespace core
  {
    ///Akin to OpenGL's VAO construct
    class VulkanVertexArray : public SequentialIdentifier
    {
    public:
      static const int MaxBindings = 16, MaxAttributes = 16;

      VulkanVertexArray(VkDevice device);
      ~VulkanVertexArray();

      void setAttribute(int location, VulkanBufferGroup *bufferGroup, int bufferIndex, VkFormat format, int offset, int stride, bool instanced=false);
      void enableAttribute(int location, bool enabled);

      void bind(VkCommandBuffer commandBuffer);

      //If it chokes on these, then build a new attrs array on the fly based on enabledBindingStates true/false
      int getNumBindings();
      const VkVertexInputBindingDescription *getBindings();

      int getNumAttributes();
      const VkVertexInputAttributeDescription *getAttributes();

      //realllly not thrilled about having to add this
      inline bool isDirty() { return bindingsDirty; }

    protected:
      VkDevice device;

      struct BindingInfo
      {
        bool instanced;
        int stride;
        VulkanBufferGroup *buffer;
        int bufferIndex;
      };

      VkVertexInputBindingDescription bindingDescriptions[MaxBindings];
      VkVertexInputAttributeDescription attributeDescriptions[MaxAttributes], enabledAttributeDescriptions[MaxAttributes];
      BindingInfo bindingInfo[MaxBindings];
      bool enabledAttributeStates[MaxAttributes], bindingsDirty = true;
      VulkanBufferGroup *bufferBindings[MaxBindings];
      int bufferBindingIndices[MaxBindings];
      int numBindings = 0, numAttributes = 0, numDefinedAttributes = 0;

      void updateBindings();
   };

    typedef VulkanVertexArray VertexArray;
  }
}