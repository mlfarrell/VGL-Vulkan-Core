#pragma once

#include <vector>
#include "vulkan.h"
#include "VulkanBufferGroup.h"

namespace vgl
{
  namespace core
  {
    ///Akin to OpenGL's VAO construct
    class VulkanVertexArray
    {
    public:
      VulkanVertexArray(VkDevice device);
      ~VulkanVertexArray();

      void setAttribute(int binding, int location, VulkanBufferGroup *bufferGroup, int bufferIndex, VkFormat format, int offset, int stride, bool instanced=false);

      void bind(VkCommandBuffer commandBuffer);

      inline int getNumBindings() const { return numBindings; }
      inline const VkVertexInputBindingDescription *getBindings() const { return bindingDescriptions; }

      inline int getNumAttributes() const { return numAttributes; }
      inline const VkVertexInputAttributeDescription *getAttributes() const { return attributeDescriptions; }

    protected:
      VkDevice device;
      VkVertexInputBindingDescription bindingDescriptions[16];
      VkVertexInputAttributeDescription attributeDescriptions[16];
      VulkanBufferGroup *buffers[16];
      int bufferIndices[16];
      int numBindings = 0, numAttributes = 0;
   };
  }
}