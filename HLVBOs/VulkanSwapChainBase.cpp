#include "pch.h"
#include "VulkanSwapChainBase.h"
#include "VulkanInstance.h"
#include "VulkanFrameBuffer.h"

#ifdef max
#undef max
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanSwapChainBase::VulkanSwapChainBase(VulkanInstance *instance)
      : instance(instance)
    {
    }

    VulkanSwapChainBase::~VulkanSwapChainBase()
    {
      cleanup();
    }

    void VulkanSwapChainBase::cleanup()
    {
      for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
      {
        vkDestroySemaphore(instance->getDefaultDevice(), renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(instance->getDefaultDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(instance->getDefaultDevice(), frameFences[i], nullptr);
      }
      vkDestroySwapchainKHR(instance->getDefaultDevice(), swapChain, nullptr);
      vkDestroySurfaceKHR(instance->getInstance(), surface, nullptr);
    }

    uint32_t VulkanSwapChainBase::acquireNextImage()
    {
      if(frameIds[currentFrame])
      {
        vkWaitForFences(swapchainDevice, 1, &frameFences[currentFrame], VK_TRUE, numeric_limits<uint64_t>::max());
        vkResetFences(swapchainDevice, 1, &frameFences[currentFrame]);
        VulkanFrameBuffer::completeFrame(frameIds[currentFrame]);
      }
      frameIds[currentFrame] = VulkanFrameBuffer::getNewFrameId();

      uint32_t imageIndex;
      vkAcquireNextImageKHR(swapchainDevice, swapChain, numeric_limits<uint64_t>::max(), imageAvailableSemaphores[currentFrame], 
        VK_NULL_HANDLE, &imageIndex);

      return imageIndex;
    }

    void VulkanSwapChainBase::submitAndPresent(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
      VkSubmitInfo submitInfo = {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

      VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
      VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = waitSemaphores;
      submitInfo.pWaitDstStageMask = waitStages;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &commandBuffer;

      VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = signalSemaphores;

      if(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameFences[currentFrame]) != VK_SUCCESS) 
        throw runtime_error("Failed to submit vulkan command buffer!");

      //present/////////////////////
      VkPresentInfoKHR presentInfo = {};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = signalSemaphores;

      VkSwapchainKHR swapChains[] = { swapChain };
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = swapChains;
      presentInfo.pImageIndices = &imageIndex;
      presentInfo.pResults = nullptr;

      if(vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS)
        throw runtime_error("Failed to present vulkan swap chain image!");

      currentFrame++;
      currentFrame %= MAX_FRAMES_IN_FLIGHT;
    }

    void VulkanSwapChainBase::init()
    {
      auto device = instance->getPhysicalDevice();

      VkBool32 presentSupport = false;
      int presentQueueFamily = instance->getGraphicsQueueFamily();
      vkGetPhysicalDeviceSurfaceSupportKHR(device, presentQueueFamily, surface, &presentSupport);

      //https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Window_surface
      if(!presentSupport)
      {
        throw runtime_error("Cannot use a Vulkan device that does not support both drawing and presenting to the swap chain!");
      }

      vkGetDeviceQueue(instance->getDefaultDevice(), presentQueueFamily, 0, &presentQueue);

      auto swapChainSupport = querySwapChainSupport(device);
      bool swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();

      graphicsQueue = instance->getGraphicsQueue();

      if(!swapChainAdequate)
      {
        throw runtime_error("Cannot use a Vulkan device that has missing swap chain formats or present modes");
      }

      createSwapchain();
    }

    VulkanSwapChainBase::SwapChainSupportDetails VulkanSwapChainBase::querySwapChainSupport(VkPhysicalDevice device)
    {
      SwapChainSupportDetails details;

      if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities) != VK_SUCCESS)
      {
        throw runtime_error("Cannot query Vulkan surface capabilities");
      }

      uint32_t formatCount;
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
      if(formatCount != 0) 
      {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
      }

      uint32_t presentModeCount;
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
      if(presentModeCount != 0) 
      {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
      }

      return details;
    }

    VkSurfaceFormatKHR VulkanSwapChainBase::chooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR> &availableFormats)
    {
      if(availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) 
      {
        return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      }

      for(const auto &availableFormat : availableFormats) 
      {
        if(availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
          return availableFormat;        
      }

      return availableFormats[0];
    }

    VkPresentModeKHR VulkanSwapChainBase::chooseSwapPresentMode(const vector<VkPresentModeKHR> &availablePresentModes)
    {
      VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

      for(const auto &availablePresentMode : availablePresentModes) 
      {
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
          return availablePresentMode;        
        else if(availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) 
          bestMode = availablePresentMode;        
      }

      return bestMode;
    }

    VkExtent2D VulkanSwapChainBase::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
      VkExtent2D extent;

      if(capabilities.currentExtent.width == numeric_limits<uint32_t>::max())
      {
        extent.width = winW;
        extent.height = winH;
      }
      else
      {
        //if this is defined, then you must use this value
        extent = capabilities.currentExtent;
      }

      return extent;
    }

    void VulkanSwapChainBase::createSwapchain()
    {
      int graphicsQueueFamily = instance->getGraphicsQueueFamily();
      int presentQueueFamily = graphicsQueueFamily;
      auto device = instance->getPhysicalDevice();
      SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);

      VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
      VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
      VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

      uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
      if(swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) 
      {
        imageCount = swapChainSupport.capabilities.maxImageCount;
      }

      VkSwapchainCreateInfoKHR createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
      createInfo.surface = surface;
      createInfo.minImageCount = imageCount;
      createInfo.imageFormat = surfaceFormat.format;
      createInfo.imageColorSpace = surfaceFormat.colorSpace;
      createInfo.imageExtent = extent;
      createInfo.imageArrayLayers = 1;
      createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

      if(graphicsQueueFamily != presentQueueFamily) 
      {
        uint32_t queueFamilyIndices[] = { (uint32_t)graphicsQueueFamily, (uint32_t)presentQueueFamily };

        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
      }
      else 
      {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
      }

      createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
      createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

      createInfo.presentMode = presentMode;
      createInfo.clipped = VK_TRUE;

      createInfo.oldSwapchain = VK_NULL_HANDLE;

      if(vkCreateSwapchainKHR(instance->getDefaultDevice(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) 
      {
        throw runtime_error("Failed to create Vulkan swap chain!");
      }

      swapChainImageFormat = surfaceFormat.format;
      swapChainExtent = extent;

      obtainSwapchainImages();
      createSyncPrimitives();
    }

    void VulkanSwapChainBase::obtainSwapchainImages()
    {
      auto device = instance->getDefaultDevice();

      uint32_t imageCount = 0;
      vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
      swapChainImages.resize(imageCount);
      vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

      swapchainDevice = device;
    }

    void VulkanSwapChainBase::createSyncPrimitives()
    {
      VkSemaphoreCreateInfo semaphoreInfo = {};
      semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

      for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
      {
        if(vkCreateSemaphore(swapchainDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
           vkCreateSemaphore(swapchainDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
           vkCreateFence(swapchainDevice, &fenceInfo, nullptr, &frameFences[i]) != VK_SUCCESS)
        {
          throw runtime_error("Failed to create Vulkan swapchain semaphores!");
        }
        frameIds[i] = 0;
      }
    }
  }
}