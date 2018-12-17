#pragma once

#include <memory>
#ifdef MACOSX
#include <SDL2/SDL.h>
#else
#include "SDL.h"
#endif
#include "ExampleRenderer.h"
#include "vglcore.h"

class Example
{
public:
  Example();
  ~Example();

  void run();

  int framesRendered = 0;
protected:
  SDL_Window *window;
  int screenW = 0, screenH = 0;
  float t = 0;

  bool done = false, paused = false;

  void setupVk();
  void reshape();

  bool processEvents();
  void update();
  void doRender();

  ExampleRenderer *renderer = nullptr;
  std::shared_ptr<vgl::core::VulkanShaderProgram> vkShader1, vkShader2;
  std::shared_ptr<vgl::core::VulkanBufferGroup> vkVbo, vkEbo, vkUbo;
  std::shared_ptr<vgl::core::VulkanVertexArray> vkVao;
  std::shared_ptr<vgl::core::VulkanTexture> vkTexture;
};
