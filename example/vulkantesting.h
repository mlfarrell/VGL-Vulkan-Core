#pragma once

#include <memory>
#ifdef MACOSX
#include <SDL2/SDL.h>
#else
#include "SDL.h"
#endif
#include "VulkanInstance.h"
#include "VulkanFrameBuffer.h"
#include "VulkanPipeline.h"
#include "VulkanShaderProgram.h"
#include "VulkanBufferGroup.h"
#include "VulkanVertexArray.h"
#include "VulkanTexture.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDescriptorPool.h"
#include "VulkanPipelineState.h"

class VGLPPVkDemo
{
public:
  VGLPPVkDemo();
  ~VGLPPVkDemo();

  void run();

  int framesRendered = 0;
protected:
  SDL_Window *window;
  SDL_GLContext context;
  int screenW = 0, screenH = 0;

  bool done = false, paused = false;

  void setupVk();
  void initPipelineState();
  void reshape();

  bool processEvents();
  void update();
  void doRender();

  void copyTextureDS(int i, VkDescriptorSet dest);

  vgl::core::VulkanPipelineState pipelineState;

  std::shared_ptr<vgl::core::VulkanInstance> vkInstance;
  std::shared_ptr<vgl::core::VulkanFrameBuffer> vkSwapchainFramebuffers, vkOffscreenFramebuffer;
  std::shared_ptr<vgl::core::VulkanShaderProgram> vkShader1, vkShader2;
  std::shared_ptr<vgl::core::VulkanPipeline> vkPipeline1, vkPipeline2;
  std::shared_ptr<vgl::core::VulkanBufferGroup> vkVbo, vkEbo, vkUbo, vkUboDynamic;
  std::shared_ptr<vgl::core::VulkanVertexArray> vkVao;
  std::shared_ptr<vgl::core::VulkanTexture> vkTexture, vkCubeTexture, vkFboDepth;
  vgl::core::VulkanTexture *vkFboColorTarget[3];
  std::shared_ptr<vgl::core::VulkanDescriptorSetLayout> vkSetLayout;
  std::shared_ptr<vgl::core::VulkanDescriptorPool> vkDescriptorPool;

  VkDescriptorSet descriptorSets[4];
};
