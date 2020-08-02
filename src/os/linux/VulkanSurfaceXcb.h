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

#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

namespace vgl
{
  namespace core
  {
    class VulkanInstance;

    class VulkanSurfaceLinux
    {
    public:
      struct LinuxWindow
      {
        xcb_connection_t *connection;
        xcb_window_t window;
      };
    
      static void setLinuxWindow(LinuxWindow lwnd);

      VulkanSurfaceLinux(VulkanInstance *instance);
      ~VulkanSurfaceLinux();

      inline VkSurfaceKHR get() { return surface; }

      void updateDimensions();
      inline uint32_t getWidth() { return w; }
      inline uint32_t getHeight() { return h; }

    protected:
      static LinuxWindow lwnd;

      VulkanInstance *instance;
      VkSurfaceKHR surface;
      uint32_t w = 0, h = 0;

      void createSurface();
    };

    typedef VulkanSurfaceLinux VulkanSurface;
  }
}