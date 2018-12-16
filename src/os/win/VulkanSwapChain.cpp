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

using namespace std;

namespace vgl
{
  namespace core
  {
    HWND VulkanSwapChainWin::hwnd = NULL;

    void VulkanSwapChainWin::setHWND(HWND hwnd)
    {
      VulkanSwapChainWin::hwnd = hwnd;
    }

    VulkanSwapChainWin::VulkanSwapChainWin(VulkanInstance *instance)
      : VulkanSwapChainBase(instance)
    {
      if(!hwnd)
        throw vgl_runtime_error("You must call VulkanSwapChain::setHWND(HWND hwnd) before initializing vulkan swap chain");

      RECT rect;
      if(GetClientRect(hwnd, &rect))
      {
        winW = rect.right-rect.left;
        winH = rect.bottom-rect.top;
      }

      createSurface();
      init();
    }

    void VulkanSwapChainWin::createSurface()
    {
      VkWin32SurfaceCreateInfoKHR createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
      createInfo.hwnd = hwnd;
      createInfo.hinstance = GetModuleHandle(nullptr);

      if(vkCreateWin32SurfaceKHR(instance->get(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan window surface!");
    }

    VulkanSwapChainWin::~VulkanSwapChainWin()
    {
    }

    void VulkanSwapChainWin::recreate()
    {
      vkDeviceWaitIdle(swapchainDevice);
      cleanup();
      createSurface();
      init();
    }
  }
}
