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

#include "vulkan.h"

namespace vgl
{
  namespace core
  {
    class VulkanExtensionLoader
    {
    public:
      //instance
      static void resolveInstanceExtensions(VkInstance instance);

      static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
      static PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
      static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
      static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;

      static PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
      static PFN_vkDebugReportMessageEXT vkDebugReportMessageEXT;
      static PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;

      //device
      static void resolveDeviceExtensions(VkInstance instance, VkDevice device);

      static PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
      static PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
      static PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
      static PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
      static PFN_vkQueuePresentKHR vkQueuePresentKHR;
    };
  }
}