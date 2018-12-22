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
#include "ExampleRenderer.h"
#include "VulkanInstance.h"
#include "VulkanShaderProgram.h"
#include "VulkanFrameBuffer.h"
#include "VulkanTexture.h"
#include "VulkanVertexArray.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDescriptorPool.h"

//shut up a you face... (https://www.youtube.com/watch?v=jqHg4w7qi0o&feature=youtu.be&t=9)
#ifdef _MSC_VER
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif

using namespace std;
using namespace vgl::core;

ExampleRenderer::ExampleRenderer()
{
  instance = new VulkanInstance();
  swapchainFramebuffers = new VulkanFrameBuffer(instance->getSwapChain(), true);

  memset(textureBindings2D, 0, sizeof(VulkanTexture *)*16);

  //starting out with a ~1 MB dynamic UBO buffer per frame
  totalDynamicUboSize = (3<<20);

  int numSwapChainImages = instance->getSwapChain()->getNumImages();
  dynamicUbos = new VulkanBufferGroup(instance->getDefaultDevice(), instance->getTransferCommandPool(), instance->getGraphicsQueue(), 
    numSwapChainImages);
  dynamicUbos->setUsageType(VulkanBufferGroup::UT_UNIFORM_DYNAMIC);
  dynamicUbos->setPreferredDeviceLocal(false);
  dynamicUbos->setDedicatedAllocation(true, totalDynamicUboSize + 8192);
  for(int i = 0; i < numSwapChainImages; i++)
    dynamicUbos->data(i, nullptr, totalDynamicUboSize / numSwapChainImages);

  initPipelineState();
  initResourceMonitorThread();
  initCommonLayoutsAndSets();
}

ExampleRenderer::~ExampleRenderer()
{
  resourceThreadEnabled = false;
  resourceThread->join();
  delete resourceThread;

  if(commonPLLayout1)
    vkDestroyPipelineLayout(instance->getDefaultDevice(), commonPLLayout1, nullptr);

  delete dynamicUbos;
  delete commonDSPoolA;
  delete commonDSPoolB;
  delete commonDSLayout1A;
  delete commonDSLayout1B;
  delete swapchainFramebuffers;
  delete instance;
}
  
void ExampleRenderer::initPipelineState()
{
  memset(&pipelineState, 0, sizeof(pipelineState));

  auto swapChainExtent = instance->getSwapChain()->getExtent();
  pipelineState.viewport0.x = 0.0f;
  pipelineState.viewport0.y = 0.0f;
  pipelineState.viewport0.width = (float)swapChainExtent.width;
  pipelineState.viewport0.height = (float)swapChainExtent.height;
  pipelineState.viewport0.minDepth = 0.0f;
  pipelineState.viewport0.maxDepth = 1.0f;
  viewport = { 0, 0, (int)swapChainExtent.width, (int)swapChainExtent.height };

  pipelineState.scissor0.offset = { 0, 0 };
  pipelineState.scissor0.extent = { (uint32_t)pipelineState.viewport0.width, (uint32_t)pipelineState.viewport0.height };

  pipelineState.viewport.viewportCount = 1;
  pipelineState.viewport.scissorCount = 1;

  pipelineState.rasterizer.depthClampEnable = VK_FALSE;
  pipelineState.rasterizer.rasterizerDiscardEnable = VK_FALSE;
  pipelineState.rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  pipelineState.rasterizer.lineWidth = 1.0f;
  pipelineState.rasterizer.cullMode = VK_CULL_MODE_NONE;
  pipelineState.rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  pipelineState.rasterizer.depthBiasEnable = VK_FALSE;
  pipelineState.rasterizer.depthBiasConstantFactor = 0.0f;
  pipelineState.rasterizer.depthBiasClamp = 0.0f;
  pipelineState.rasterizer.depthBiasSlopeFactor = 0.0f;

  pipelineState.depthStencil.depthTestEnable = VK_FALSE;
  pipelineState.depthStencil.depthWriteEnable = VK_TRUE;
  pipelineState.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  pipelineState.depthStencil.depthBoundsTestEnable = VK_FALSE;

  pipelineState.multiSample.sampleShadingEnable = VK_FALSE;
  pipelineState.multiSample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  pipelineState.multiSample.minSampleShading = 1.0f;
  pipelineState.multiSample.alphaToCoverageEnable = VK_FALSE;
  pipelineState.multiSample.alphaToOneEnable = VK_FALSE;

  pipelineState.blendAttachment0.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  pipelineState.blendAttachment0.blendEnable = VK_FALSE;
  pipelineState.blendAttachment0.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  pipelineState.blendAttachment0.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  pipelineState.blendAttachment0.colorBlendOp = VK_BLEND_OP_ADD;
  pipelineState.blendAttachment0.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  pipelineState.blendAttachment0.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  pipelineState.blendAttachment0.alphaBlendOp = VK_BLEND_OP_ADD;

  pipelineState.blend.logicOpEnable = VK_FALSE;
  pipelineState.blend.logicOp = VK_LOGIC_OP_COPY;
  pipelineState.blend.attachmentCount = 1;
  pipelineState.blend.blendConstants[0] = 0.0f;
  pipelineState.blend.blendConstants[1] = 0.0f;
  pipelineState.blend.blendConstants[2] = 0.0f;
  pipelineState.blend.blendConstants[3] = 0.0f;

  pipelineState.inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

void ExampleRenderer::initResourceMonitorThread()
{
  resourceThreadEnabled = true;

  //down the road, switch to a 100% sleep-based mechanic
  resourceThread = new thread([=] {
    while(resourceThreadEnabled.load())
    {
      instance->getResourceMonitor()->poll(instance->getDefaultDevice());
      this_thread::sleep_for(chrono::milliseconds(10));
    }
  });
}

void ExampleRenderer::initCommonLayoutsAndSets()
{
  int numSwapChainImages = instance->getSwapChain()->getNumImages();

  //we'll use 1a for uniforms & 1b for textures / input images
  core::VulkanDescriptorSetLayout::Binding vtxUbo = { 0, 1 }, txtSampler = { 0, 16 }, none = {};
  commonDSLayout1A = new core::VulkanDescriptorSetLayout(instance->getDefaultDevice(), vtxUbo, none, none, true);
  commonDSLayout1B = new core::VulkanDescriptorSetLayout(instance->getDefaultDevice(), none, none, txtSampler, true);
  commonDSPoolA = new core::VulkanDescriptorPool(instance->getDefaultDevice(), 16, 0, 16, 0, 0);
  commonDSPoolB = new core::VulkanDescriptorPool(instance->getDefaultDevice(), 1024, 0, 0, 1024, 0);

  VkDescriptorSetLayout dsLayouts[2] = { commonDSLayout1A->get(), commonDSLayout1B->get() };
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 2;
  pipelineLayoutInfo.pSetLayouts = dsLayouts;
  pipelineLayoutInfo.pushConstantRangeCount = 0;
  pipelineLayoutInfo.pPushConstantRanges = nullptr;

  if(vkCreatePipelineLayout(instance->getDefaultDevice(), &pipelineLayoutInfo, nullptr, &commonPLLayout1) != VK_SUCCESS)
    throw vgl_runtime_error("Failed to create common pipeline layout!");

  if(!commonDSPoolA->simpleAllocate(numSwapChainImages, commonDSLayout1A, dynamicUboSets))
    throw vgl_runtime_error("Failed to allocate common pipeline descriptor sets!");

  for(int i = 0; i < numSwapChainImages; i++)
  {
    dynamicUbos->putDescriptor(i, dynamicUboSets[i], 0);
  }

  if(!commonDSPoolB->simpleAllocate(1, commonDSLayout1B, &templateState1DS))
    throw vgl_runtime_error("Failed to allocate common pipeline descriptor sets!");
}

void ExampleRenderer::copySet1DS()
{
  VkCopyDescriptorSet copy = {};
  copy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;

  copy.srcSet = templateState1DS;
  copy.srcBinding = 0;
  copy.srcArrayElement = 0;
  copy.dstSet = currentSet1DS;
  copy.dstBinding = 0;
  copy.dstArrayElement = 0;
  copy.descriptorCount = maxTextureBinding2D+1;

  vkUpdateDescriptorSets(instance->getDefaultDevice(), 0, nullptr, 1, &copy);
}

void ExampleRenderer::setBlendFuncSourceFactor(BlendFactor srcFactor, BlendFactor dstFactor)
{
  VkBlendFactor blendFactors[] = {
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
  };
    
  pipelineState.blendAttachment0.srcColorBlendFactor = blendFactors[(int)srcFactor];
  pipelineState.blendAttachment0.srcAlphaBlendFactor = blendFactors[(int)srcFactor];
  pipelineState.blendAttachment0.dstColorBlendFactor = blendFactors[(int)dstFactor];
  pipelineState.blendAttachment0.dstAlphaBlendFactor = blendFactors[(int)dstFactor];

  psoDirty = true;
}

void ExampleRenderer::enableMultisampling(bool multisampling, bool sampleShading, int numSamples, bool alphaToCoverage, bool alphaToOne, float minSampleShading)
{
  pipelineState.multiSample.sampleShadingEnable = sampleShading ? VK_TRUE :VK_FALSE;
  pipelineState.multiSample.rasterizationSamples = multisampling ? (VkSampleCountFlagBits)numSamples : VK_SAMPLE_COUNT_1_BIT;
  pipelineState.multiSample.minSampleShading = minSampleShading;
  pipelineState.multiSample.alphaToCoverageEnable = alphaToCoverage ? VK_TRUE : VK_FALSE;
  pipelineState.multiSample.alphaToOneEnable = alphaToOne ? VK_TRUE : VK_FALSE;

  psoDirty = true;
}

void ExampleRenderer::setShaders(VulkanShaderProgram *shaders)
{
  if(currentShaders != shaders)
  {
    currentShaders = shaders;
    psoDirty = true;
  }
}

void ExampleRenderer::updateShaderDynamicUBO(int uboIndex, int offset, void *uboState, int len)
{
  uint64_t currentFrameId = instance->getSwapChain()->getCurrentFrameId();

  if(currentShaders->getDynamicUbos() != dynamicUbos)
  {
    currentShaders->setDynamicUBOs(dynamicUbos, instance->getSwapChain()->getNumImages());
  }

  currentDynamicUboOffset = currentDynamicUboEnd;
  if(!currentShaders->updateDynamicUboState(currentFrameImage, uboIndex, offset, uboState, len, currentDynamicUboEnd))
  {
    recoverFromDynamicUBOOverflow();

    currentShaders->setDynamicUBOs(dynamicUbos, instance->getSwapChain()->getNumImages());
    if(!currentShaders->updateDynamicUboState(currentFrameImage, uboIndex, offset, uboState, len, currentDynamicUboEnd))
      throw vgl_runtime_error("Unable to update dynamic UBO data required for current Vulkan shader");
  }
  clDynamicUboDirty = true;
}

void ExampleRenderer::setCurrentVertexArray(core::VulkanVertexArray *vao)
{
  currentVertexArray = vao;
}

void ExampleRenderer::setInputLayout(core::VulkanVertexArray *vao)
{
  pipelineState.numVertexAttributes = vao->getNumAttributes();
  pipelineState.numVertexBindings = vao->getNumBindings();
  memcpy(pipelineState.vertexInputBindings, vao->getBindings(), sizeof(VkVertexInputBindingDescription)*pipelineState.numVertexBindings);
  memcpy(pipelineState.vertexInputAttributes, vao->getAttributes(), sizeof(VkVertexInputAttributeDescription)*pipelineState.numVertexAttributes);
  psoDirty = true;
}

void ExampleRenderer::setRenderTarget(core::VulkanFrameBuffer *fbo)
{
  currentFramebuffer = fbo;
}

void ExampleRenderer::setTextureBinding2D(VulkanTexture *tex, int binding)
{
  textureBindings2D[binding] = tex;

  if(tex && !tex->isUndefined())
  {
    tex->putDescriptor(templateState1DS, binding);
    textureBindingBits2D |= (1<<binding);
    maxTextureBinding2D = max((uint16_t)binding, maxTextureBinding2D);
  }
  else
  {
    textureBindingBits2D &= ~(1<<binding);
  }
  clTextureSetDirty = true;
}

void ExampleRenderer::setCubemapBinding(VulkanTexture *tex, int binding)
{
  setTextureBinding2D(tex, binding);
}

void ExampleRenderer::preparePipelineForCoreState(VkCommandBuffer commandBuffer)
{
  if(psoDirty)
  {
    currentPipeline = currentShaders->pipelineForState(pipelineState, currentFramebuffer);

    psoDirty = false;
    clPsoDirty = true;
  }

  if(clPsoDirty)
  {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->get());
    clPsoDirty = false;
  }
}

void ExampleRenderer::recoverFromDescriptorPoolOverflow()
{
  //we ran out of descriptor sets in the current pool mid-frame!
  //lets create a bigger one and async release the old one (when frame is done)

  auto resourceMonitor = instance->getResourceMonitor();
  auto newRenderPool = new VulkanDescriptorPool(currentRenderPool, 2);
  auto currentPoolHandle = currentRenderPool->asyncReleaseOwnership();
  uint64_t frame = instance->getSwapChain()->getCurrentFrameId();

  VulkanAsyncResourceCollection frameResources(resourceMonitor, frame, { currentPoolHandle });
  resourceMonitor->append(move(frameResources));
  currentPoolHandle->release();

  swapchainFramebuffers->replaceDescriptorPool(currentFrameImage, newRenderPool);
  currentRenderPool = newRenderPool;
}

void ExampleRenderer::recoverFromDynamicUBOOverflow()
{
  int numSwapChainImages = instance->getSwapChain()->getNumImages();
  uint64_t frame = instance->getSwapChain()->getCurrentFrameId();
  auto resourceMonitor = instance->getResourceMonitor();

  //Quick note, right now this is unbounded.  So if the user happened to go ballistic with draw calls,
  //we could eat up a TON of memory.  Those on /r/vulkan say this is okay... i'm still skeptical
  totalDynamicUboSize *= 2;

  //Another note.. this buffer is split up and by ALL swapchain images at once!  So lets schedule it for a few frames ahead
  dynamicUbos->retainResourcesUntilFrameCompletion(frame+(uint64_t)numSwapChainImages);
  delete dynamicUbos;

  dynamicUbos = new VulkanBufferGroup(instance->getDefaultDevice(), instance->getTransferCommandPool(), instance->getGraphicsQueue(),
    numSwapChainImages);
  dynamicUbos->setUsageType(VulkanBufferGroup::UT_UNIFORM_DYNAMIC);
  dynamicUbos->setPreferredDeviceLocal(false);
  dynamicUbos->setDedicatedAllocation(true, totalDynamicUboSize + 8192);
  for(int i = 0; i < numSwapChainImages; i++)
    dynamicUbos->data(i, nullptr, totalDynamicUboSize / numSwapChainImages);

  //We have to recreate the common pool & DS for dynamic UBOs now as well
  auto currentPoolHandle = commonDSPoolA->asyncReleaseOwnership();

  VulkanAsyncResourceCollection frameResources(resourceMonitor, frame+(uint64_t)numSwapChainImages, { currentPoolHandle });
  resourceMonitor->append(move(frameResources));
  currentPoolHandle->release();
  delete commonDSPoolA;

  commonDSPoolA = new VulkanDescriptorPool(instance->getDefaultDevice(), 16, 0, 16, 0, 0);
  if(!commonDSPoolA->simpleAllocate(numSwapChainImages, commonDSLayout1A, dynamicUboSets))
    throw vgl_runtime_error("Failed to allocate common pipeline descriptor sets!");

  for(int i = 0; i < numSwapChainImages; i++)
  {
    dynamicUbos->putDescriptor(i, dynamicUboSets[i], 0);
  }

  if(DebugBuild())
  {
    vout << "Upping dynamic UBO memory to " << totalDynamicUboSize + 8192 << " in response to dynamic UBO buffers overflow" << endl;
  }
}

void ExampleRenderer::prepareToDraw()
{
  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();

  if(currentRenderVertexArray != currentVertexArray || currentRenderVertexArray->isDirty())
  {
    if(!commandBuffer)
      commandBuffer = getRenderingCommandBuffer();

    currentRenderVertexArray = currentVertexArray;
    currentRenderVertexArray->bind(commandBuffer);

    setInputLayout(currentRenderVertexArray);
  }

  if(clDynamicUboDirty)
  {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentShaders->getPipelineLayout(), 0, 1, 
      &dynamicUboSets[currentFrameImage], 1, &currentDynamicUboOffset);
    clDynamicUboDirty = false;
  }

  if(clTextureSetDirty && textureBindingBits2D)
  {
    if(currentRenderPool)
    {
      if(!currentRenderPool->simpleAllocate(1, commonDSLayout1B, &currentSet1DS))
      {
        recoverFromDescriptorPoolOverflow();

        if(!currentRenderPool->simpleAllocate(1, commonDSLayout1B, &currentSet1DS))
          throw vgl_runtime_error("Unable to allocate descriptor sets required for Vulkan frame");
      }
      clTextureBindingsDirty = true;
    }

    copySet1DS();
    clTextureSetDirty = false;
  }

  if(clTextureBindingsDirty)
  {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentShaders->getPipelineLayout(), 1, 1, 
      &currentSet1DS, 0, nullptr);
    clTextureBindingsDirty = false;
  }
}

void ExampleRenderer::waitForRender()
{
  vkQueueWaitIdle(instance->getGraphicsQueue());

  //if this gives me trouble, do a gfx queue wait idle instead (it did)
  //instance->getSwapChain()->waitForRender();
}

void ExampleRenderer::presentAndSwapBuffers(bool waitForFrame)
{
  if(!instance->getSwapChain()->presentImage(currentFrameImage))
  {
    //swapchain out of date?  no idea what to do here yet
    throw runtime_error("Vulkan swapchain out of date (no handler for this yet)");
  }
  previousFrameImage = currentFrameImage;
  currentFrameImage = -1;

  if(waitForFrame)
    waitForRender();
}

int ExampleRenderer::getLastRenderedSwapchainImageIndex()
{
  return currentFrameImage;
}
  
void ExampleRenderer::enableDepthTesting(bool b)
{
  pipelineState.depthStencil.depthTestEnable = b;
  psoDirty = true;
}
  
void ExampleRenderer::setDepthFunc(DepthFunc func)
{
  switch(func)
  {
    case DF_LESS:
      pipelineState.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    break;
    case DF_LEQUAL:
      pipelineState.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    break;
    case DF_GREATER:
      pipelineState.depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
    break;
    case DF_GEQUAL:
      pipelineState.depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    break;
  }
  psoDirty = true;
}
  
void ExampleRenderer::setDepthRange(float rmin, float rmax)
{
  pipelineState.viewport0.minDepth = rmin;
  pipelineState.viewport0.maxDepth = rmax;
  psoDirty = true;
}
  
void ExampleRenderer::setViewport(int x, int y, int w, int h)
{
  viewport = { x, y, w, h };
  pipelineState.viewport0.x = x;
  pipelineState.viewport0.y = y+h;
  pipelineState.viewport0.width = w;
  pipelineState.viewport0.height = -h;
  psoDirty = true;
}
  
int4 ExampleRenderer::getViewport()
{
  return viewport;
}

void ExampleRenderer::setColorMask(bool r, bool g, bool b, bool a)
{
  pipelineState.blendAttachment0.colorWriteMask = 0;

  if(r)
    pipelineState.blendAttachment0.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
  if(g)
    pipelineState.blendAttachment0.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
  if(b)
    pipelineState.blendAttachment0.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
  if(a)
    pipelineState.blendAttachment0.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;

  psoDirty = true;
}
  
void ExampleRenderer::setDepthMask(bool mask)
{
  pipelineState.depthStencil.depthWriteEnable = (mask) ? VK_TRUE : VK_FALSE;
  psoDirty = true;
}
  
void ExampleRenderer::setCullFace(bool cullFace)
{
  if(cullFace)
    pipelineState.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  else
    pipelineState.rasterizer.cullMode = VK_CULL_MODE_NONE;
  psoDirty = true;
}
  
bool ExampleRenderer::enableBlending(bool b)
{
  if(b != blendingOn)
  {
    if(b)
      pipelineState.blendAttachment0.blendEnable = VK_TRUE;
    else
      pipelineState.blendAttachment0.blendEnable = VK_FALSE;
      
    blendingOn = b;
    psoDirty = true;
      
    //state was changed
    return true;
  }
    
  return false;
}
  
bool ExampleRenderer::isBlendingEnabled()
{
  return blendingOn;
}
  
void ExampleRenderer::setFrontFaceCounterClockwise(bool ffccw)
{
  pipelineState.rasterizer.frontFace = (ffccw) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
  psoDirty = true;
}

void ExampleRenderer::drawIndexedPrimitives(PrimitiveType type, size_t count, IndexFormat format, size_t bufferOffsetInBytes)
{
  if(!count)
    return;

  static const VkPrimitiveTopology mode[] =  
  {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, //Note:  this was LINE_LOOP, which vulkan cannot handle natively
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST
  };
  const VkPrimitiveTopology topology = mode[(int)type];

  uint32_t startIndexLocation = 0;
  if(bufferOffsetInBytes)
  {
    size_t indSz = 0;
    if(format == IF_USHORT)
    {
      indSz = sizeof(unsigned short);
    }
    else
    {
      indSz = sizeof(unsigned int);
    }

    startIndexLocation = bufferOffsetInBytes / indSz;
  }

  if(pipelineState.inputAssembly.topology != topology)
  {
    pipelineState.inputAssembly.topology = topology;
    psoDirty = true;
  }

  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();
  preparePipelineForCoreState(commandBuffer);
  vkCmdDrawIndexed(commandBuffer, count, 1, startIndexLocation, 0, 0);
}
  
void ExampleRenderer::drawPrimitiveArray(PrimitiveType type, size_t count, size_t offsetInElements)
{
  if(!count)
    return;

  static const VkPrimitiveTopology mode[] =
  {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, //Note:  this was LINE_LOOP, which vulkan cannot handle natively
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST
  };
  const VkPrimitiveTopology topology = mode[(int)type];

  if(pipelineState.inputAssembly.topology != topology)
  {
    pipelineState.inputAssembly.topology = topology;
    psoDirty = true;
  }

  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();
  preparePipelineForCoreState(commandBuffer);
  vkCmdDraw(commandBuffer, count, 1, offsetInElements, 0);
}

void ExampleRenderer::drawInstancedIndexedPrimitives(PrimitiveType type, size_t count, IndexFormat format, size_t bufferOffsetInBytes, int instanceCount)
{
  if(!count)
    return;

  static const VkPrimitiveTopology mode[] =
  {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, //Note:  this was LINE_LOOP, which vulkan cannot handle natively
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST
  };
  const VkPrimitiveTopology topology = mode[(int)type];

  uint32_t startIndexLocation = 0;
  if(bufferOffsetInBytes)
  {
    size_t indSz = 0;
    if(format == IF_USHORT)
    {
      indSz = sizeof(unsigned short);
    }
    else
    {
      indSz = sizeof(unsigned int);
    }

    startIndexLocation = bufferOffsetInBytes / indSz;
  }

  if(pipelineState.inputAssembly.topology != topology)
  {
    pipelineState.inputAssembly.topology = topology;
    psoDirty = true;
  }

  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();
  preparePipelineForCoreState(commandBuffer);
  vkCmdDrawIndexed(commandBuffer, count, instanceCount, startIndexLocation, 0, 0);
}

void ExampleRenderer::drawInstancedPrimitiveArray(PrimitiveType type, size_t count, size_t offsetInElements, int instanceCount)
{
  if(!count)
    return;

  static const VkPrimitiveTopology mode[] =
  {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, //Note:  this was LINE_LOOP, which vulkan cannot handle natively
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST
  };
  const VkPrimitiveTopology topology = mode[(int)type];

  if(pipelineState.inputAssembly.topology != topology)
  {
    pipelineState.inputAssembly.topology = topology;
    psoDirty = true;
  }

  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();
  preparePipelineForCoreState(commandBuffer);
  vkCmdDraw(commandBuffer, count, instanceCount, offsetInElements, 0);
}

void ExampleRenderer::beginFrame()
{
  if(auto transfer = instance->getCurrentTransferCommandBuffer().first)
    endSetup(false);

  uint32_t i = instance->getSwapChain()->acquireNextImage();

  auto commandBuffer = swapchainFramebuffers->getCommandBuffer(i);

  currentRenderPool = swapchainFramebuffers->getCurrentDescriptorPool(i);
  currentDynamicUboOffset = 0;
  currentDynamicUboEnd = 0;
  clPsoDirty = true;
  clDynamicUboDirty = true;
  clTextureSetDirty = true;

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  beginInfo.pInheritanceInfo = nullptr;

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

  renderPassInfo.renderArea.offset = { 0, 0 };

  VkClearValue clearValues[2];
  clearValues[1].depthStencil = { 1.0f, 0 };
  renderPassInfo.clearValueCount = 2;
  renderPassInfo.pClearValues = clearValues;

  if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
  {
    throw runtime_error("failed to begin recording command buffer!");
  }

  renderPassInfo.renderPass = swapchainFramebuffers->getRenderPass();
  renderPassInfo.framebuffer = swapchainFramebuffers->get(i);
  clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
  renderPassInfo.renderArea.extent = instance->getSwapChain()->getExtent();
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  currentRenderVertexArray = nullptr;
  currentRenderFramebuffer = nullptr;
  currentFrameImage = (int)i;
  currentSwapchainCommandBuffer = commandBuffer;
}

void ExampleRenderer::endFrame()
{
  if(auto transfer = instance->getCurrentTransferCommandBuffer().first)
    endSetup(false);

  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();
  vkCmdEndRenderPass(commandBuffer);

  if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    throw runtime_error("Failed to record vulkan command buffer!");

  instance->getSwapChain()->submitCommands(commandBuffer);

  currentSwapchainCommandBuffer = VK_NULL_HANDLE;
  instance->setCurrentRenderingCommandBuffer(VK_NULL_HANDLE);
  currentRenderVertexArray = nullptr;
  currentRenderPool = nullptr;
  currentRenderFramebuffer = nullptr;
}

VkCommandBuffer ExampleRenderer::getRenderingCommandBuffer()
{
  if(currentRenderFramebuffer != currentFramebuffer)
  {
    if(currentRenderFramebuffer && renderingOffscreenFramebuffer)
    {
      endOffscreenFrame();
    }

    currentRenderFramebuffer = currentFramebuffer;

    if(currentFramebuffer != swapchainFramebuffers)
    {
      beginOffscreenFrame();
    }
    else
    {
      instance->setCurrentRenderingCommandBuffer(currentSwapchainCommandBuffer);
    }
  }

  return instance->getCurrentRenderingCommandBuffer();
}

void ExampleRenderer::beginOffscreenFrame()
{
  auto commandBuffer = currentRenderFramebuffer->getCommandBuffer();

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  beginInfo.pInheritanceInfo = nullptr;

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = currentRenderFramebuffer->getSize();

  if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
  {
    throw runtime_error("failed to begin recording command buffer!");
  }

  renderPassInfo.renderPass = currentRenderFramebuffer->getRenderPass();
  renderPassInfo.framebuffer = currentRenderFramebuffer->get();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  instance->setCurrentRenderingCommandBuffer(commandBuffer);
  renderingOffscreenFramebuffer = true;
  currentRenderVertexArray = nullptr;
}

void ExampleRenderer::endOffscreenFrame()
{
  auto commandBuffer = instance->getCurrentRenderingCommandBuffer();

  vkCmdEndRenderPass(commandBuffer);
  if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    throw runtime_error("Failed to record offscreen vulkan command buffer!");

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  if(vkQueueSubmit(instance->getGraphicsQueue(), 1, &submitInfo, currentRenderFramebuffer->getFence()) != VK_SUCCESS)
    throw runtime_error("Failed to submit a vulkan command buffer!");

  renderingOffscreenFramebuffer = false;
  currentRenderVertexArray = nullptr;
}

void ExampleRenderer::beginSetup()
{
  //setup already started.. we could next these with a stack, but for now we'll flush directly
  //and break
  if(instance->getCurrentTransferCommandBuffer().first)
    return;

  instance->beginTransferCommands();
}

void ExampleRenderer::endSetup(bool wait)
{
  auto transferFence = instance->getCurrentTransferCommandBuffer().second;

  instance->endTransferCommands();
  if(wait)
    vkWaitForFences(instance->getDefaultDevice(), 1, &transferFence, VK_TRUE, numeric_limits<uint64_t>::max());
}

VkCommandBuffer ExampleRenderer::getSetupCommandBuffer()
{
  return instance->getTransferCommandBuffer().first;
}

