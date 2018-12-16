/*************************************************************************************
Basic example program demonstrating how to use a few of the Vulkan Core's features
Forgive me, I haven't had a ton of time to clean up in here
Eventually, a better demo of the core's capability will replace this.
*************************************************************************************/

#include "pch.h"
#include <linalg.h>
#include "VecTypes.h"
#include "MatTypes.h"
#include "VulkanTesting.h"
#include "VulkanAsyncResourceHandle.h"
#include <memory>
#include <iostream>
#include <sstream>
#include <fstream>
#include <thread>
#ifdef WIN32
#include <Windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "VulkanInstance.h"
#include "VulkanSwapChain.h"

#ifdef WIN32
#include <SDL_syswm.h>
#endif
#ifdef MACOSX
#include <SDL2/SDL_syswm.h>
#include "VulkanTestingMac.h"
#endif

using namespace std;
using namespace vgl::core;
using namespace linalg;

static const mat4 mat4Identity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

static Uint32 timerCallback(Uint32 interval, void *param);

static mat4 toMat4(const linalg::mat<float, 4, 4> &lmat)
{
  mat4 ret;

  for(int j = 0; j < 4; j++) for(int i = 0; i < 4; i++)
    ret.m[i*4+j] = lmat[i][j];
  return ret;
}

VGLPPVkDemo::VGLPPVkDemo()
{
  context = NULL;
  int mode = 0;
  int flags[] = { 0, SDL_WINDOW_FULLSCREEN, SDL_WINDOW_FULLSCREEN_DESKTOP };

  SDL_Init(SDL_INIT_VIDEO);

  const int w = 1280, h = 720;
  window = SDL_CreateWindow("VGL Vulkan Core Demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags[mode]);

  screenW = w;
  screenH = h;
  setupVk();

  //reshape();

  SDL_AddTimer(1000, timerCallback, this);
}

static Uint32 timerCallback(Uint32 interval, void *param)
{
  VGLPPVkDemo *demo = (VGLPPVkDemo *)param;

  ostringstream ostr;
  ostr << "FPS: " << demo->framesRendered << endl;
#ifdef WIN32
  OutputDebugStringA(ostr.str().c_str());
#else
  cout << ostr.str();
#endif

  demo->framesRendered = 0;
  return interval;
}

void VGLPPVkDemo::setupVk()
{
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if(!SDL_GetWindowWMInfo(window, &info))
  {
    throw runtime_error("All is lost");
  }

  VulkanSwapChain::InitParameters swapChainParams = {};
  swapChainParams.mailboxPresentMode = true;
  swapChainParams.depthBits = 24;
  VulkanSwapChain::setInitParameters(swapChainParams);
#ifdef WIN32
  VulkanSwapChain::setHWND(info.info.win.window);
#elif defined MACOSX
  addMetalLayer(info.info.cocoa.window);
  VulkanSwapChain::setWindow(info.info.cocoa.window);
#endif

  vkInstance = make_shared<VulkanInstance>();
  vkSwapchainFramebuffers = make_shared<VulkanFrameBuffer>(vkInstance->getSwapChain());

  const int numSwapChainImages = vkInstance->getSwapChain()->getNumImages();
  assert(numSwapChainImages <= 3);

  auto device = vkInstance->getDefaultDevice();
  auto transferPool = vkInstance->getTransferCommandPool();
  auto queue = vkInstance->getGraphicsQueue();

  initPipelineState();

  vkShader1 = make_shared<VulkanShaderProgram>(device);

  vkShader1->addShaderGLSL(VulkanShaderProgram::ST_VERTEX, R"glsl(#version 450
    #extension GL_ARB_separate_shader_objects : enable

    layout(location = 0) in vec4 inPosition;
    layout(location = 1) in vec2 inTexcoord;
    layout(location = 0) out vec2 texcoord;

    layout(set = 0, binding = 0) uniform UniformBufferObject {
        mat4 model;
        mat4 view;
        mat4 proj;
    } ubo;

    out gl_PerVertex {
        vec4 gl_Position;
    };

    void main() {
        gl_Position = ubo.proj * ubo.view * ubo.model * inPosition;
        texcoord = inTexcoord;// * 10.0;
    })glsl");

  vkShader1->addShaderGLSL(VulkanShaderProgram::ST_FRAGMENT, R"glsl(#version 450
    #extension GL_ARB_separate_shader_objects : enable

    layout(location = 0) in vec2 texcoord;
    layout(location = 0) out vec4 outColor;
    layout(set = 0, binding = 1) uniform sampler2D tex;

    void main() {
        outColor = texture(tex, texcoord);
    })glsl");

  auto getSource = [](const string &path) {
    ifstream file(path, ios::ate | ios::binary);

    if(!file.is_open())
      return string();

    size_t fileSize = (size_t)file.tellg();
    char *data = new char[fileSize+1];

    file.seekg(0);
    file.read((char *)data, fileSize);

    data[fileSize] = 0;
    string ret(data);
    delete []data;

    return ret;
  };

  vkShader2 = make_shared<VulkanShaderProgram>(device);
  vkShader2->addShaderGLSL(VulkanShaderProgram::ST_VERTEX, getSource("glsl/cubemap.vert"));
  vkShader2->addShaderGLSL(VulkanShaderProgram::ST_FRAGMENT, getSource("glsl/cubemap.frag"));

  const float vertices[] = {
    -1.0f, -1.0f, 1.0f,
    1.0f, -1.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f,
    1.0f, 1.0f, -1.0f,
    -1.0f, 1.0f, -1.0f,
  };
  const float texcoords[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
  };

  uint32_t inds[] = {
    0, 1, 2,
    2, 3, 0,
    1, 5, 6,
    6, 2, 1,
    7, 6, 5,
    5, 4, 7,
    4, 0, 3,
    3, 7, 4,
    4, 5, 1,
    1, 0, 4,
    3, 2, 6,
    6, 7, 3, 
  };

  struct ubo 
  {
    mat4 model;
    mat4 view;
    mat4 proj;
  } ubo;

  auto quat = rotation_quat({ 1, 0, 0 }, (float)-M_PI/4);
  ubo.model = toMat4(rotation_matrix(quat));
  ubo.view = toMat4(translation_matrix(vec<float, 3>{ 0.0f, 0.0f, -2.0f }));
  ubo.proj = toMat4(perspective_matrix((float)(M_PI/4.0f), screenW/(float)screenH, 0.1f, 1000.0f, linalg::neg_z, linalg::zero_to_one));

  vkVbo = make_shared<VulkanBufferGroup>(device, transferPool, queue, 2);
  vkVbo->data(0, vertices, sizeof(vertices));
  vkVbo->data(1, texcoords, sizeof(texcoords));
  vkEbo = make_shared<VulkanBufferGroup>(device, transferPool, queue, 1);
  vkEbo->setUsageType(VulkanBufferGroup::UT_INDEX);
  vkEbo->data(0, inds, sizeof(inds));
  vkUbo = make_shared<VulkanBufferGroup>(device, transferPool, queue, 1);
  vkUbo->setUsageType(VulkanBufferGroup::UT_UNIFORM);
  vkUbo->setDeviceLocal(false);
  vkUbo->data(0, &ubo, sizeof(ubo));

  vkUboDynamic = make_shared<VulkanBufferGroup>(device, transferPool, queue, numSwapChainImages);
  vkUboDynamic->setUsageType(VulkanBufferGroup::UT_UNIFORM_DYNAMIC);
  vkUboDynamic->setDeviceLocal(false);
  vkUboDynamic->setDedicatedAllocation(true, (3<<20) + 8192);
  for(int i = 0; i < numSwapChainImages; i++)
    vkUboDynamic->data(i, nullptr, (3<<20) / numSwapChainImages);

  //this isn't right, but just testing for now
  vkShader2->setDynamicUBOs(vkUboDynamic.get(), numSwapChainImages);

  vkVao = make_shared<VulkanVertexArray>(vkInstance->getDefaultDevice());
  vkVao->setAttribute(0, vkVbo.get(), 0, VK_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float)*3);
  vkVao->setAttribute(1, vkVbo.get(), 1, VK_FORMAT_R32G32_SFLOAT, 0, sizeof(float)*2);
  vkVao->enableAttribute(0, true);
  vkVao->enableAttribute(1, true);

  auto createTex = [=] {
    int texWidth, texHeight, texChannels;
    stbi_uc *pixels = stbi_load("banana.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if(!pixels) 
    {
      throw runtime_error("Failed to load texture image!");
    }

    vkTexture = make_shared<VulkanTexture>(device, VulkanTexture::TT_2D, transferPool, queue);
    vkTexture->setMipmap(true);
    vkTexture->setFilters(VulkanTexture::ST_LINEAR, VulkanTexture::ST_LINEAR_MIPMAP_LINEAR);
    vkTexture->imageData(texWidth, texHeight, 1, VK_FORMAT_R8G8B8A8_UNORM, pixels, imageSize);

    //cubemap testing
    vkCubeTexture = make_shared<VulkanTexture>(device, VulkanTexture::TT_CUBE_MAP, transferPool, queue);
    vkCubeTexture->setMipmap(true);
    vkCubeTexture->setFilters(VulkanTexture::ST_LINEAR, VulkanTexture::ST_LINEAR_MIPMAP_LINEAR);
    
    for(int i = 0; i < 6; i++)
    {
      vkCubeTexture->imageData(texWidth, texHeight, 1, VK_FORMAT_R8G8B8A8_UNORM, pixels, imageSize, i);
    }

    stbi_image_free(pixels);
  };
  createTex();

  vkDescriptorPool = make_shared<VulkanDescriptorPool>(device, 32, 0, 16, 16, 0);

  VulkanDescriptorSetLayout::Binding vtxUbo = { 0, 1 }, txtSampler = { 1, 1 }, none = {};
  vkSetLayout = make_shared<VulkanDescriptorSetLayout>(device, vtxUbo, none, txtSampler, true);

  assert(numSwapChainImages <= 3);
  vkDescriptorPool->simpleAllocate(numSwapChainImages, vkSetLayout.get(), descriptorSets);
  for(int i = 0; i < numSwapChainImages; i++)
  {
    vkUboDynamic->putDescriptor(i, descriptorSets[i], 0);
    vkCubeTexture->putDescriptor(descriptorSets[i], 1);
  }

  VulkanDescriptorSetLayout *layouts[] = { vkSetLayout.get() };
  vkPipeline1 = make_shared<VulkanPipeline>(device, &pipelineState, vkSwapchainFramebuffers->getRenderPass(),
    vkShader1.get(), vkVao.get(), layouts, 1, vkInstance->getPipelineCache());

  vkPipeline2 = make_shared<VulkanPipeline>(device, &pipelineState, vkSwapchainFramebuffers->getRenderPass(),
    vkShader2.get(), vkVao.get(), layouts, 1, vkInstance->getPipelineCache());
}

void VGLPPVkDemo::initPipelineState()
{
  memset(&pipelineState, 0, sizeof(pipelineState));

  auto swapChainExtent = vkInstance->getSwapChain()->getExtent();
  pipelineState.viewport0.x = 0.0f;
  pipelineState.viewport0.y = 0.0f;
  pipelineState.viewport0.width = (float)swapChainExtent.width;
  pipelineState.viewport0.height = (float)swapChainExtent.height;
  pipelineState.viewport0.minDepth = 0.0f;
  pipelineState.viewport0.maxDepth = 1.0f;

  pipelineState.scissor0.offset = { 0, 0 };
  pipelineState.scissor0.extent = { (uint32_t)pipelineState.viewport0.width, (uint32_t)pipelineState.viewport0.height };

  pipelineState.viewport.viewportCount = 1;
  pipelineState.viewport.scissorCount = 1;

  pipelineState.rasterizer.depthClampEnable = VK_FALSE;
  pipelineState.rasterizer.rasterizerDiscardEnable = VK_FALSE;
  pipelineState.rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  pipelineState.rasterizer.lineWidth = 1.0f;
  pipelineState.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  pipelineState.rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  pipelineState.rasterizer.depthBiasEnable = VK_FALSE;
  pipelineState.rasterizer.depthBiasConstantFactor = 0.0f;
  pipelineState.rasterizer.depthBiasClamp = 0.0f;
  pipelineState.rasterizer.depthBiasSlopeFactor = 0.0f;

  pipelineState.depthStencil.depthTestEnable = VK_TRUE;
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

void VGLPPVkDemo::reshape()
{
  //this hasn't been worked on yet (and will probably break)
  vkInstance->getSwapChain()->recreate();

  //this destroys the render pass, which we should keep between recreation
  vkSwapchainFramebuffers = make_shared<VulkanFrameBuffer>(vkInstance->getSwapChain());

  auto swapChainExtent = vkInstance->getSwapChain()->getExtent();
  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)swapChainExtent.width;
  viewport.height = (float)swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
}

void VGLPPVkDemo::run()
{
  while(!done)
  {
    //Check for events
    done = processEvents();

    if(paused)
    {
      this_thread::sleep_for(chrono::milliseconds(1));
      continue;
    }

    //Do logic updates
    update();

    //Render display
    doRender();
    framesRendered++;

    vkInstance->getResourceMonitor()->poll(vkInstance->getDefaultDevice());
  }
}

bool VGLPPVkDemo::processEvents()
{
  SDL_Event event;
  bool done = false;

  while(SDL_PollEvent(&event))
  {
    switch(event.type)
    {
      case SDL_WINDOWEVENT_CLOSE:
      {
        if(window)
        {
          SDL_DestroyWindow(window);
          window = NULL;
          done = true;
        }
      }
      break;
      case SDL_WINDOWEVENT_MINIMIZED:
        //these events aren't working for some reason...
        paused = true;
      break;
      case SDL_WINDOWEVENT_RESTORED:
        paused = false;
      break;
      case SDL_MOUSEMOTION:
        if(event.motion.state)
        {
        }
      break;
      case SDL_KEYDOWN:
        //non-game keys
        switch(event.key.keysym.sym)
        {
        case SDLK_ESCAPE:
          done = true;
          break;
        }
      break;
      case SDL_QUIT:
        //quit out of the program
        done = true;
      break;
    }
  }

  const Uint8 *key = SDL_GetKeyboardState(NULL);

  return done;
}

void VGLPPVkDemo::update()
{
}

static mat4 mat4MakePerspective(float fovyRadians, float aspect, float nearZ, float farZ)
{
  float cotan = 1.0f / tanf(fovyRadians / 2.0f);

  mat4 m = { cotan / aspect, 0.0f, 0.0f, 0.0f,
    0.0f, cotan, 0.0f, 0.0f,
    0.0f, 0.0f, farZ / (nearZ - farZ), -1.0f,
    0.0f, 0.0f, -(farZ * nearZ) / (farZ - nearZ), 0.0f };

  return m;
}

void VGLPPVkDemo::doRender()
{
  uint32_t i = vkInstance->getSwapChain()->acquireNextImage();
  auto commandBuffer = vkSwapchainFramebuffers->getCommandBuffer(i);

  struct ubo
  {
    mat4 model;
    mat4 view;
    mat4 proj;
  } ubo;

  static float t = 0;
  t += 0.01f;

  //VkDescriptorSet uboDescriptorSets[1];

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

  memset(vkFboColorTarget, 0, sizeof(VulkanTexture *)*3);
      
  if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
  {
    throw runtime_error("failed to begin recording command buffer!");
  }

  renderPassInfo.renderPass = vkSwapchainFramebuffers->getRenderPass();
  renderPassInfo.framebuffer = vkSwapchainFramebuffers->get(i);
  clearValues[0].color = { 0.0f, 0.0f, 0.4f, 1.0f };
  renderPassInfo.renderArea.extent = vkInstance->getSwapChain()->getExtent();
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  uint32_t currentDynamicUboEnd = 0;

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline2->get());
  vkVao->bind(commandBuffer);
  vkEbo->bindIndices(0, commandBuffer);

  ubo.view = toMat4(translation_matrix(vec<float, 3>{ 0, 0, -1 }));
  ubo.proj = toMat4(perspective_matrix((float)(M_PI/4.0f), screenW/(float)screenH, 0.1f, 1000.0f, linalg::neg_z, linalg::zero_to_one));

  static const int cubeSpan = 20;
  uint32_t uboOffset = 0;
  for(int x = -cubeSpan; x < cubeSpan; x++) for(int y = -cubeSpan; y < cubeSpan; y++)
  {
    const float sc = 0.05f;
    auto rotation = rotation_matrix(rotation_quat({ 0, 1, 0 }, t));
    auto scaling = scaling_matrix(vec<float, 3>{ sc, sc, sc });

    ubo.model = toMat4(mul(mul(translation_matrix(vec<float, 3>{ x/6.0f, y/6.0f, 0 }), rotation), scaling));
    uboOffset = currentDynamicUboEnd;
    if(!vkShader2->updateDynamicUboState(i, 0, 0, &ubo, sizeof(ubo), currentDynamicUboEnd))
      throw runtime_error("Dyanmic UBO state overflow!");
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline2->getLayout(), 0, 1, &descriptorSets[i], 1, &uboOffset);
    vkCmdDrawIndexed(commandBuffer, 36, 1, 0, 0, 0);
  }

  vkCmdEndRenderPass(commandBuffer);

  if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    throw runtime_error("failed to record command buffer!");

  if(!vkInstance->getSwapChain()->submitAndPresent(commandBuffer, i))
    paused = true;
}

VGLPPVkDemo::~VGLPPVkDemo()
{
  vkInstance->waitForDeviceIdle();
  vkVbo = vkEbo = vkUbo = nullptr;
  vkUboDynamic = nullptr;
  vkTexture = nullptr;
  vkCubeTexture = nullptr;
  vkFboDepth = nullptr;
  for(int i = 0; i < 3; i++)
    delete vkFboColorTarget[i];
  vkSetLayout = nullptr;
  vkSwapchainFramebuffers = nullptr;
  vkOffscreenFramebuffer = nullptr;
  vkPipeline1 = vkPipeline2 = nullptr;
  vkShader1 = vkShader2 = nullptr;
  vkDescriptorPool = nullptr;
  vkInstance = nullptr;

  //close and destroy the window
  SDL_DestroyWindow(window);

  //clean up
  SDL_Quit();
}

int main(int argc, char *argv[])
{
  auto vkTesting = make_unique<VGLPPVkDemo>();
  vkTesting->run();
  vkTesting.reset();

  return 0;
}
