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
    VulkanSurfaceLinux::LinuxWindow VulkanSurfaceLinux::lwnd = {};

    void VulkanSurfaceLinux::setLinuxWindow(LinuxWindow lwnd)
    {
      VulkanSurfaceLinux::lwnd = lwnd;
    }

    VulkanSurfaceLinux::VulkanSurfaceLinux(VulkanInstance *instance) : instance(instance)
    {
      if(!lwnd.dpy || !lwnd.window)
        throw vgl_runtime_error("You must call VulkanSwapChain::setLinuxWindow() before initializing vulkan swap chain");

      updateDimensions();
      createSurface();
    }

    void VulkanSurfaceLinux::createSurface()
    {
      VkXlibSurfaceCreateInfoKHR createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
      createInfo.dpy = lwnd.dpy;
      createInfo.window = lwnd.window;

      if(vkCreateXlibSurfaceKHR(instance->get(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan window surface!");
    }

    void VulkanSurfaceLinux::updateDimensions()
    {    
      Window root;
      int x, y;
      uint32_t bw, d;
      
      XGetGeometry(lwnd.dpy, lwnd.window, &root, &x, &y, &w, &h, &bw, &d);
    }

    VulkanSurfaceLinux::~VulkanSurfaceLinux()
    {
      vkDestroySurfaceKHR(instance->get(), surface, nullptr);
    }
  }
}
