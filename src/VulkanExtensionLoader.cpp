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
#include "VulkanExtensionLoader.h"

namespace vgl
{
  namespace core
  {
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR VulkanExtensionLoader::vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR VulkanExtensionLoader::vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR VulkanExtensionLoader::vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR VulkanExtensionLoader::vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;

    PFN_vkCreateDebugReportCallbackEXT VulkanExtensionLoader::vkCreateDebugReportCallbackEXT = nullptr;
    PFN_vkDebugReportMessageEXT VulkanExtensionLoader::vkDebugReportMessageEXT = nullptr;
    PFN_vkDestroyDebugReportCallbackEXT VulkanExtensionLoader::vkDestroyDebugReportCallbackEXT = nullptr;

    PFN_vkCreateSwapchainKHR VulkanExtensionLoader::vkCreateSwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR VulkanExtensionLoader::vkGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR VulkanExtensionLoader::vkAcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR VulkanExtensionLoader::vkQueuePresentKHR = nullptr;
    PFN_vkDestroySwapchainKHR VulkanExtensionLoader::vkDestroySwapchainKHR = nullptr;

    template <typename M>
    void getProc(VkInstance instance, M &method, const char *name)
    {
      method = (M)(vkGetInstanceProcAddr(instance, name));
    }

    template <typename M>
    void getProc(VkDevice device, M &method, const char *name)
    {
      method = (M)(vkGetDeviceProcAddr(device, name));
    }

    void VulkanExtensionLoader::resolveInstanceExtensions(VkInstance instance)
    {
      getProc(instance, vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
      getProc(instance, vkGetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
      getProc(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
      getProc(instance, vkGetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");

      getProc(instance, vkCreateDebugReportCallbackEXT, "vkCreateDebugReportCallbackEXT");
      getProc(instance, vkDebugReportMessageEXT, "vkDebugReportMessageEXT");
      getProc(instance, vkDestroyDebugReportCallbackEXT, "vkDestroyDebugReportCallbackEXT");
    }

    void VulkanExtensionLoader::resolveDeviceExtensions(VkInstance instance, VkDevice device)
    {
      getProc(device, vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
      getProc(device, vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
      getProc(device, vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
      getProc(device, vkQueuePresentKHR, "vkQueuePresentKHR");
      getProc(device, vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
    }
  }
}