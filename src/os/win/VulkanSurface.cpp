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
#include "VulkanSwapChain.h"
#include "VulkanExtensionLoader.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    HWND VulkanSurfaceWin::hwnd = NULL;

    void VulkanSurfaceWin::setHWND(HWND hwnd)
    {
      VulkanSurfaceWin::hwnd = hwnd;
    }

    VulkanSurfaceWin::VulkanSurfaceWin(VulkanInstance *instance) : instance(instance)
    {
      if(!hwnd)
        throw vgl_runtime_error("You must call VulkanSwapChain::setHWND(HWND hwnd) before initializing vulkan swap chain");

      updateDimensions();
      createSurface();
    }

    void VulkanSurfaceWin::createSurface()
    {
      VkWin32SurfaceCreateInfoKHR createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
      createInfo.hwnd = hwnd;
      createInfo.hinstance = GetModuleHandle(nullptr);

      if(vkCreateWin32SurfaceKHR(instance->get(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan window surface!");
    }

    void VulkanSurfaceWin::updateDimensions()
    {
      RECT rect;
      if(GetClientRect(hwnd, &rect))
      {
        w = rect.right-rect.left;
        h = rect.bottom-rect.top;
      }
    }

    VulkanSurfaceWin::~VulkanSurfaceWin()
    {
      vkDestroySurfaceKHR(instance->get(), surface, nullptr);
    }
  }
}
