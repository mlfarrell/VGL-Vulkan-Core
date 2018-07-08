#pragma once

#include "VulkanSwapChainBase.h"

namespace vgl
{
  namespace core
  {
    class VulkanSwapChainWin : public VulkanSwapChainBase
    {
    public:
      static void setHWND(HWND hwnd);

      VulkanSwapChainWin(VulkanInstance *instance);
      ~VulkanSwapChainWin();

      void recreate();

    protected:
      static HWND hwnd;

      void createSurface();
    };

    typedef VulkanSwapChainWin VulkanSwapChain;
  }
}