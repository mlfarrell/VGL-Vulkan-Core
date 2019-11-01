/********************************************************************************************
Basic example program demonstrating how to use a renderer built on top of the VGL Vulkan Core.
This is not intended to show the optimal way to use vulkan, but instead the EASIEST way
to use it to build an OpenGL-style graphics engine.
********************************************************************************************/

#include "pch.h"
#include <linalg.h>
#include "VecTypes.h"
#include "MatTypes.h"
#include "Example.h"
#include "ExampleModelData.h"
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

///This is specific to the UBO used in the shader UBO block
///Both of our shaders use this same UBO block layout..
struct UboData1
{
  mat4 model;
  mat4 view;
  mat4 proj;
  float4 diffuseColor;
};
UboData1 uboData1;

static Uint32 timerCallback(Uint32 interval, void *param);

static mat4 toMat4(const linalg::mat<float, 4, 4> &lmat)
{
  mat4 ret;

  for(int j = 0; j < 4; j++) for(int i = 0; i < 4; i++)
    ret.m[i*4+j] = lmat[i][j];
  return ret;
}

Example::Example()
{
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
  Example *demo = (Example *)param;

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

static string getSource(const string &path) 
{
  ifstream file(path, ios::ate | ios::binary);

  if(!file.is_open())
    return string();

  size_t fileSize = (size_t)file.tellg();
  char *data = new char[fileSize+1];

  file.seekg(0);
  file.read((char *)data, fileSize);

  data[fileSize] = 0;
  string ret(data);
  delete[]data;

  return ret;
};

static void checkShaderBuild(bool result)
{
  if(!result)
  {
    throw runtime_error("One of the shaders failed to build!");
  }
}

void Example::setupVk()
{
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if(!SDL_GetWindowWMInfo(window, &info))
  {
    throw runtime_error("All is lost");
  }

  VulkanSwapChain::InitParameters swapChainParams = {};
  swapChainParams.mailboxPresentMode = false; //use true for vsync-off (FIFO -> mailbox)
  swapChainParams.depthBits = 24;
  VulkanSwapChain::setInitParameters(swapChainParams);
#ifdef WIN32
  VulkanSurface::setHWND(info.info.win.window);
#elif defined MACOSX
  addMetalLayer(info.info.cocoa.window);
  VulkanSurface::setWindow(info.info.cocoa.window);
#endif

  renderer = new ExampleRenderer();

  auto &instance = core::VulkanInstance::currentInstance();
  auto device = instance.getDefaultDevice();
  auto queue = instance.getGraphicsQueue();
  auto transferCb = instance.getTransferCommandBuffer().first;
  auto transferPool = instance.getTransferCommandPool();

  vkShader1 = make_shared<VulkanShaderProgram>(device);
  vkShader1->setPipelineLayout(renderer->getCommonPLLayout1());
  checkShaderBuild(vkShader1->addShaderGLSL(VulkanShaderProgram::ST_VERTEX, getSource("glsl/lighting.vert")));
  checkShaderBuild(vkShader1->addShaderGLSL(VulkanShaderProgram::ST_FRAGMENT, getSource("glsl/lighting.frag")));

  vkShader2 = make_shared<VulkanShaderProgram>(device);
  vkShader2->setPipelineLayout(renderer->getCommonPLLayout1());
  checkShaderBuild(vkShader2->addShaderGLSL(VulkanShaderProgram::ST_VERTEX, getSource("glsl/lightingTex.vert")));
  checkShaderBuild(vkShader2->addShaderGLSL(VulkanShaderProgram::ST_FRAGMENT, getSource("glsl/lightingTex.frag")));

  vkVbo = make_shared<VulkanBufferGroup>(device, (VkCommandPool)VK_NULL_HANDLE, (VkQueue)VK_NULL_HANDLE, 3);
  vkVbo->data(0, modelVerts, sizeof(modelVerts), transferCb);
  vkVbo->data(1, modelNorms, sizeof(modelNorms), transferCb);
  vkVbo->data(2, modelTexcoords, sizeof(modelTexcoords), transferCb);
  vkEbo = make_shared<VulkanBufferGroup>(device, (VkCommandPool)VK_NULL_HANDLE, (VkQueue)VK_NULL_HANDLE, 1);
  vkEbo->setUsageType(VulkanBufferGroup::UT_INDEX);
  vkEbo->data(0, modelIndices, sizeof(modelIndices), transferCb);

  vkVao = make_shared<VulkanVertexArray>(instance.getDefaultDevice());
  vkVao->setAttribute(0, vkVbo.get(), 0, VK_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float)*3);
  vkVao->setAttribute(1, vkVbo.get(), 1, VK_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float)*3);
  vkVao->setAttribute(2, vkVbo.get(), 2, VK_FORMAT_R32G32_SFLOAT, 0, sizeof(float)*2);
  vkVao->enableAttribute(0, true);
  vkVao->enableAttribute(1, true);
  vkVao->enableAttribute(2, true);

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

    stbi_image_free(pixels);
  };
  createTex();
}

void Example::reshape()
{
  //this hasn't been worked on yet (and will probably break)
}

void Example::run()
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
  }
}

bool Example::processEvents()
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

void Example::update()
{
  t += 0.01f;
}

void Example::doRender()
{
  renderer->beginFrame();

  uboData1.view = toMat4(translation_matrix(vec<float, 3>{ 0.0f, 0.0f, -10.0f }));
  uboData1.proj = toMat4(perspective_matrix((float)(M_PI/4.0f), screenW/(float)screenH, 0.1f, 1000.0f, linalg::neg_z, linalg::zero_to_one));

  renderer->enableDepthTesting(true);

  renderer->setRenderTarget(renderer->getSwapchainFramebuffers());
  renderer->setCurrentVertexArray(vkVao.get());
  renderer->setShaders(vkShader1.get());
  vkEbo->bindIndices(0, renderer->getRenderingCommandBuffer());

  float4 materialDiffuseColors[3] =
  {
    float4(0, 0, 1, 1), float4(1, 0, 0, 1), float4(1, 1, 1, 1)
  };

  for(int i = 0; i < 3; i++)
  {
    float offset = (float)(i-1)*4.0f;
    auto modelTranslation = translation_matrix(vec<float, 3>{ offset, 0, 0 });
    auto modelBaseTransform = mul(scaling_matrix(vec<float, 3>{ 0.1f, 0.1f, 0.1f }), 
      rotation_matrix(rotation_quat({ 1.0f, 0.0f, 0.0f }, (float)M_PI/2)));
    auto modelAnimTransform = rotation_matrix(rotation_quat({ 0.0f, 1.0f, 0.0f }, (float)M_PI+t+offset));

    uboData1.model = toMat4(mul(modelTranslation, mul(modelAnimTransform, modelBaseTransform)));
    uboData1.diffuseColor = materialDiffuseColors[i];

    if(i == 2)
    {
      //use a textured (different) shader for third object
      renderer->setTextureBinding2D(vkTexture.get(), 0);
      renderer->setShaders(vkShader2.get());
    }

    renderer->updateShaderDynamicUBO(0, 0, &uboData1, sizeof(uboData1));
    renderer->prepareToDraw();

    renderer->drawIndexedPrimitives(PT_TRIANGLES, sizeof(modelIndices)/sizeof(uint32_t));
  }

  renderer->endFrame();
  renderer->presentAndSwapBuffers(false);
}

Example::~Example()
{
  renderer->waitForRender();
  vkVbo = vkEbo = vkUbo = nullptr;
  vkTexture = nullptr;
  vkShader1 = vkShader2 = nullptr;
  delete renderer;

  //close and destroy the window
  SDL_DestroyWindow(window);

  //clean up
  SDL_Quit();
}

int main(int argc, char *argv[])
{
  auto vkTesting = make_unique<Example>();
  vkTesting->run();
  vkTesting.reset();

  return 0;
}
