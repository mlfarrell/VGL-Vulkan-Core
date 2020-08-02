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
      if(!lwnd.connection || !lwnd.window)
        throw vgl_runtime_error("You must call VulkanSwapChain::setLinuxWindow() before initializing vulkan swap chain");

      updateDimensions();
      createSurface();
    }

    void VulkanSurfaceLinux::createSurface()
    {
      VkXcbSurfaceCreateInfoKHR createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
      createInfo.connection = lwnd.connection;
      createInfo.window = lwnd.window;

      if(vkCreateXcbSurfaceKHR(instance->get(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to create Vulkan window surface!");
    }

    void VulkanSurfaceLinux::updateDimensions()
    {    
      xcb_get_geometry_cookie_t cookie;
      xcb_get_geometry_reply_t *reply;
      
      //this cookie tastes terrible..
      cookie = xcb_get_geometry(lwnd.connection, lwnd.window);
      
      if((reply = xcb_get_geometry_reply(lwnd.connection, cookie, NULL)))
      {
        w = reply->width;
        h = reply->height;
      }
      free(reply);           
    }

    VulkanSurfaceLinux::~VulkanSurfaceLinux()
    {
      vkDestroySurfaceKHR(instance->get(), surface, nullptr);
    }
  }
}
