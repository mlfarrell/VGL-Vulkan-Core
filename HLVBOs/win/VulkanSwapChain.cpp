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
        throw runtime_error("You must call VulkanSwapChain::setHWND(HWND hwnd) before initializing vulkan swap chain");

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

      if(vkCreateWin32SurfaceKHR(instance->getInstance(), &createInfo, NULL, &surface) != VK_SUCCESS)
        throw runtime_error("Failed to create Vulkan window surface!");
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
