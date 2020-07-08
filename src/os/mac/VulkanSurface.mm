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
#import <AppKit/AppKit.h>
#include "VulkanSwapChain.h"
#include "VulkanExtensionLoader.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"

using namespace std;

namespace vgl
{
  namespace core
  {
    void *VulkanSurfaceMac::window = NULL;

    void VulkanSurfaceMac::setWindow(void *cocoaWindow)
    {
      window = cocoaWindow;
    }

    VulkanSurfaceMac::VulkanSurfaceMac(VulkanInstance *instance) : instance(instance)
    {
      if(!window)
        throw runtime_error("You must call VulkanSwapChain::setWindow() before initializing vulkan swap chain");

      updateDimensions();
      createSurface();
    }

    void VulkanSurfaceMac::createSurface()
    {
      NSWindow *win = (__bridge NSWindow *)window;
      
      VkMacOSSurfaceCreateInfoMVK createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
      createInfo.pView = (__bridge void *)win.contentView;

      if(vkCreateMacOSSurfaceMVK(instance->get(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw runtime_error("Failed to create Vulkan window surface!");
    }

    void VulkanSurfaceMac::updateDimensions()
    {
      NSWindow *win = (__bridge NSWindow *)window;
      w = (uint32_t)win.contentView.bounds.size.width;
      h = (uint32_t)win.contentView.bounds.size.height;
    }

    VulkanSurfaceMac::~VulkanSurfaceMac()
    {
      vkDestroySurfaceKHR(instance->get(), surface, nullptr);
    }
  }
}
