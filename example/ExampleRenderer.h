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

#include <atomic>
#include <thread>
#include "VulkanPipelineState.h"
#include "VecTypes.h"

//Various abstraction enums//////////////////////////////////////////
enum PrimitiveType
{
  PT_TRIANGLES=0,
  PT_TRIANGLE_FAN,
  PT_LINES,
  PT_LINE_STRIP,
  PT_LINE_LOOP,
  PT_POINTS
};
  
enum IndexFormat
{
  IF_USHORT=0,
  IF_UINT
};

namespace vgl
{
  namespace core
  {
    class VulkanInstance;
    class VulkanShaderProgram;
    class VulkanFrameBuffer;
    class VulkanVertexArray;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanTexture;
    class VulkanPipeline;
    class VulkanBufferGroup;
  }  
}

enum BlendFactor
{
  BF_ONE = 0,
  BF_SRC_ALPHA,
  BF_ONE_MINUS_SRC_ALPHA
};

enum DepthFunc
{
  DF_LESS = 0,
  DF_LEQUAL,
  DF_GREATER,
  DF_GEQUAL
};

using namespace vgl;

class ExampleRenderer
{
public:    
  ExampleRenderer();
  virtual ~ExampleRenderer();
    
  //Pipeline state
  virtual void setColorMask(bool r, bool g, bool b, bool a);
  virtual void setDepthMask(bool mask);
  virtual void setCullFace(bool cullFace);
  virtual void setFrontFaceCounterClockwise(bool ffccw);
  virtual void enableDepthTesting(bool b);
  virtual void setDepthFunc(DepthFunc func);
  virtual void setDepthRange(float rmin, float rmax);
  virtual bool enableBlending(bool b);
  virtual bool isBlendingEnabled();
  virtual void setBlendFuncSourceFactor(BlendFactor srcFactor, BlendFactor dstFactor);
  virtual void enableMultisampling(bool multisampling, bool sampleShading, int numSamples, bool alphaToCoverage=false, bool alphaToOne=false, float minSampleShading=1.0f);

  void setShaders(core::VulkanShaderProgram *shaders);
  void updateShaderDynamicUBO(int uboIndex, int offset, void *uboState, int len);
  void setCurrentVertexArray(core::VulkanVertexArray *vao);
  void setInputLayout(core::VulkanVertexArray *vao);
  void setRenderTarget(core::VulkanFrameBuffer *fbo);
  void setTextureBinding2D(core::VulkanTexture *tex, int binding);
  void setCubemapBinding(core::VulkanTexture *tex, int binding);

  //Viewport state
  virtual void setViewport(int x, int y, int w, int h);
  virtual int4 getViewport();

  //Drawing
  void drawIndexedPrimitives(PrimitiveType type, size_t count, IndexFormat format=IF_UINT, size_t bufferOffsetInBytes=0);
  void drawPrimitiveArray(PrimitiveType type, size_t count, size_t offsetInElements=0);
  void drawInstancedIndexedPrimitives(PrimitiveType type, size_t count, IndexFormat format=IF_UINT, size_t bufferOffsetInBytes=0, int instanceCount=1);
  void drawInstancedPrimitiveArray(PrimitiveType type, size_t count, size_t offsetInElements=0, int instanceCount=1);

  //Rendering
  void preparePipelineForCoreState(VkCommandBuffer commandBuffer);
  virtual void prepareToDraw();
  virtual void waitForRender();
  virtual void presentAndSwapBuffers(bool waitForFrame);
  int getLastRenderedSwapchainImageIndex();

  virtual void beginFrame();
  virtual void endFrame();
  VkCommandBuffer getRenderingCommandBuffer();

  void beginOffscreenFrame();
  void endOffscreenFrame();

  virtual void beginSetup();
  virtual void endSetup(bool wait);
  VkCommandBuffer getSetupCommandBuffer();

  inline VkPipelineLayout getCommonPLLayout1() { return commonPLLayout1; }
  inline core::VulkanFrameBuffer *getSwapchainFramebuffers() { return swapchainFramebuffers; }

private:
  void initPipelineState();
  void initResourceMonitorThread();
  void initCommonLayoutsAndSets();
  void recoverFromDescriptorPoolOverflow();
  void recoverFromDynamicUBOOverflow();
  void recreateSwapchainFrameBuffers();

  core::VulkanInstance *instance;
  core::VulkanFrameBuffer *swapchainFramebuffers, *currentFramebuffer = nullptr, *currentRenderFramebuffer = nullptr;
  core::VulkanPipeline *currentPipeline = nullptr;
  core::VulkanPipelineState pipelineState;
  core::VulkanShaderProgram *currentShaders = nullptr;
  core::VulkanBufferGroup *dynamicUbos;
  core::VulkanVertexArray *currentVertexArray = nullptr, *currentRenderVertexArray = nullptr;
  core::VulkanDescriptorSetLayout *commonDSLayout1A, *commonDSLayout1B;
  core::VulkanDescriptorPool *commonDSPoolA, *commonDSPoolB, *currentRenderPool = nullptr;
  core::VulkanTexture *textureBindings2D[16];
  core::VulkanTexture *undefinedTexture = nullptr, *undefinedCubemap = nullptr;
  uint16_t textureBindingBits2D = 0, maxTextureBinding2D = 0;
  VkPipelineLayout commonPLLayout1;
  VkDescriptorSet dynamicUboSets[3];
  VkDescriptorSet currentSet1DS, templateState1DS;
  VkCommandBuffer currentSwapchainCommandBuffer;
  bool renderingOffscreenFramebuffer = false;

  void updateSet1DS();

  std::atomic_bool resourceThreadEnabled;
  std::thread *resourceThread;

  size_t totalDynamicUboSize = 0;
  int currentFrameImage = -1, previousFrameImage = -1;
  uint32_t currentDynamicUboOffset = 0, currentDynamicUboEnd = 0;
  VkDeviceSize nonCoherentAtomSz = 0;

  bool blendingOn = false;
  int4 viewport;
  bool psoDirty = true, clPsoDirty = false, clDynamicUboDirty = false, clTextureSetDirty = false, clTextureBindingsDirty = false;    
};
