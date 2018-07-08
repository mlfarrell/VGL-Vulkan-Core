
#include "pch.h"
#include "VulkanVertexArray.h"
#include "VulkanMemoryManager.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanVertexArray::VulkanVertexArray(VkDevice device)
      : device(device)
    {
    }

    VulkanVertexArray::~VulkanVertexArray()
    {
    }

    void VulkanVertexArray::setAttribute(int binding, int location, VulkanBufferGroup *bufferGroup, int bufferIndex, VkFormat format, int offset, int stride, bool instanced)
    {
      VkVertexInputBindingDescription bindingDescription = {};
      bindingDescription.binding = binding;
      bindingDescription.stride = stride;
      bindingDescription.inputRate = (!instanced) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
      bindingDescriptions[binding] = bindingDescription;
      numBindings = max(numBindings, binding+1);

      attributeDescriptions[location].binding = binding;
      attributeDescriptions[location].location = location;
      attributeDescriptions[location].format = format;
      attributeDescriptions[location].offset = offset;
      numAttributes = max(numAttributes, location+1);

      buffers[binding] = bufferGroup;
      bufferIndices[binding] = bufferIndex;
    }

    void VulkanVertexArray::bind(VkCommandBuffer commandBuffer)
    {
      VkBuffer vkBuffers[16];
      VkDeviceSize offsets[16] = { 0 };

      for(int i = 0; i < numBindings; i++)
        vkBuffers[i] = buffers[i]->get(bufferIndices[i]);
      vkCmdBindVertexBuffers(commandBuffer, 0, numBindings, vkBuffers, offsets);
    }
  }
}