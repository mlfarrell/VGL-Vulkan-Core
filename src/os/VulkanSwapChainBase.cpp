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
#include "VulkanSwapChainBase.h"
#include "VulkanInstance.h"
#include "VulkanFrameBuffer.h"
#include "VulkanTexture.h"

#ifdef max
#undef max
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    VulkanSwapChainBase::InitParameters VulkanSwapChainBase::initParams = {};

    void VulkanSwapChainBase::setInitParameters(InitParameters params)
    {
      initParams = params;
    }

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
      vkDestroySurfaceKHR(instance->get(), surface, nullptr);
      if(depthAttachment)
        delete depthAttachment;
      if(msaaColorTarget)
        delete msaaColorTarget;
      if(msaaDepthTarget)
        delete msaaDepthTarget;
    }

    uint32_t VulkanSwapChainBase::acquireNextImage()
    {
      if(frameIds[currentFrame])
      {
        vkWaitForFences(swapchainDevice, 1, &frameFences[currentFrame], VK_TRUE, numeric_limits<uint64_t>::max());
        vkResetFences(swapchainDevice, 1, &frameFences[currentFrame]);
        completeFrame(frameIds[currentFrame]);
      }
      frameIds[currentFrame] = getNewFrameId();

      uint32_t imageIndex;
      if(vkAcquireNextImageKHR(swapchainDevice, swapChain, numeric_limits<uint64_t>::max(), imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE, &imageIndex) != VK_SUCCESS)
      {
        return 0;
      }

      return imageIndex;
    }

    void VulkanSwapChainBase::submitCommands(VkCommandBuffer commandBuffer, bool waitOnImageAcquire)
    {
      VkSubmitInfo submitInfo = {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

      VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
      submitInfo.waitSemaphoreCount = (waitOnImageAcquire) ? 1 : 0;
      submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
      submitInfo.pWaitDstStageMask = waitStages;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &commandBuffer;
      submitInfo.signalSemaphoreCount = (waitOnImageAcquire) ? 1 : 0;
      submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

      if(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameFences[currentFrame]) != VK_SUCCESS)
        throw vgl_runtime_error("Failed to submit vulkan command buffer!");

      lastSubmittedFrame = currentFrame;

      if(!waitOnImageAcquire)
      {
        //the caller intends to not present this image
        currentFrame++;
        currentFrame %= MAX_FRAMES_IN_FLIGHT;
      }
    }

    bool VulkanSwapChainBase::presentImage(uint32_t imageIndex)
    {
      VkPresentInfoKHR presentInfo = {};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];

      VkSwapchainKHR swapChains[] = { swapChain };
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = swapChains;
      presentInfo.pImageIndices = &imageIndex;
      presentInfo.pResults = nullptr;

      VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

      if(result != VK_SUCCESS)
      {
        if(result == VK_ERROR_OUT_OF_DATE_KHR)
        {
          //pewmp
          defunct = true;
          return false;
        }
        else
        {
          throw vgl_runtime_error("Failed to present vulkan swap chain image!");
        }
      }
      else
      {
        currentFrame++;
        currentFrame %= MAX_FRAMES_IN_FLIGHT;
      }

      return true;
    }

    bool VulkanSwapChainBase::submitAndPresent(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
      submitCommands(commandBuffer);
      return presentImage(imageIndex);
    }

    void VulkanSwapChainBase::waitForRender()
    {
      vkWaitForFences(swapchainDevice, MAX_FRAMES_IN_FLIGHT, frameFences, VK_TRUE, numeric_limits<uint64_t>::max());
    }

    //this method is intended to replace the acquire step when rendering to a swapchain image that we do NOT intend to present
    void VulkanSwapChainBase::waitForPreviousFrame()
    {
      VkSubmitInfo submitInfo = {};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

      //this may look stupid, but for the intent of what this method is used for, it MUST be this way
      vkWaitForFences(swapchainDevice, MAX_FRAMES_IN_FLIGHT, frameFences, VK_TRUE, numeric_limits<uint64_t>::max());
      vkResetFences(swapchainDevice, 1, &frameFences[currentFrame]);
      frameIds[currentFrame] = getNewFrameId();
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
        throw vgl_runtime_error("Cannot use a Vulkan device that does not support both drawing and presenting to the swap chain!");
      }

      vkGetDeviceQueue(instance->getDefaultDevice(), presentQueueFamily, 0, &presentQueue);

      auto swapChainSupport = querySwapChainSupport(device);
      bool swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();

      graphicsQueue = instance->getGraphicsQueue();

      if(!swapChainAdequate)
      {
        throw vgl_runtime_error("Cannot use a Vulkan device that has missing swap chain formats or present modes");
      }

      createSwapchain();
      if(initParams.depthBits)
        createDepthAttachment();
    }

    VulkanSwapChainBase::SwapChainSupportDetails VulkanSwapChainBase::querySwapChainSupport(VkPhysicalDevice device)
    {
      SwapChainSupportDetails details;

      if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities) != VK_SUCCESS)
      {
        throw vgl_runtime_error("Cannot query Vulkan surface capabilities");
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

      if(initParams.mailboxPresentMode)
      {
        for(const auto &availablePresentMode : availablePresentModes)
        {
          if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return availablePresentMode;
          else if(availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            bestMode = availablePresentMode;
        }
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

      //use 2 to maximize latency (below aims for triple buffering)
      uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
      if(swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) 
      {
        imageCount = swapChainSupport.capabilities.maxImageCount;
      }
      if(swapChainSupport.capabilities.minImageCount > 0 && imageCount < swapChainSupport.capabilities.minImageCount)
      {
        imageCount = swapChainSupport.capabilities.minImageCount;
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

      if(initParams.allowReadingImages)
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

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
        throw vgl_runtime_error("Failed to create Vulkan swap chain!");
      }

      swapChainImageFormat = surfaceFormat.format;
      swapChainExtent = extent;

      obtainSwapchainImages();
      createSyncPrimitives();

      if(initParams.multiSamples > 1)
      {
        auto device = instance->getDefaultDevice();
        auto pool = instance->getTransferCommandPool();

        //the jury is out on whether or not I really would need 1 of these for each swapchain image
        msaaColorTarget = new VulkanTexture(device, VulkanTexture::TT_2D, pool, graphicsQueue);
        msaaColorTarget->initImage(swapChainExtent.width, swapChainExtent.height, 1, swapChainImageFormat, VulkanTexture::U_COLOR_ATTACHMENT, 0, initParams.multiSamples);

        multiSamples = (VkSampleCountFlagBits)initParams.multiSamples;
      }
    }

    void VulkanSwapChainBase::createDepthAttachment()
    {
      auto physicalDevice = instance->getPhysicalDevice();

      auto findSupportedFormat = [=](const vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) -> VkFormat {
        for(VkFormat format : candidates) 
        {
          VkFormatProperties props;
          vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

          if(tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
            return format;
          else if(tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) 
            return format;
        }

        return VK_FORMAT_UNDEFINED;
      };

      VkFormat depthFormat = VK_FORMAT_UNDEFINED;

      switch(initParams.depthBits)
      {
        case 16:
          depthFormat = findSupportedFormat({ VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, 
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        break;
        case 24:
          depthFormat = findSupportedFormat({ VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        break;
        case 32:
          depthFormat = findSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT }, VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        break;
      }

      if(depthFormat == VK_FORMAT_UNDEFINED)
      {
        vector<vector<VkFormat>> allFormats({ 
          { VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT }, 
          { VK_FORMAT_D24_UNORM_S8_UINT }, 
          { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT } 
        });

        //fallback
        for(auto &depthFormats : allFormats)
        {
          depthFormat = findSupportedFormat(depthFormats, VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
          if(depthFormat != VK_FORMAT_UNDEFINED)
            break;
        }

        if(depthFormat != VK_FORMAT_UNDEFINED)
          vout << "Unable to find Vulkan depth format that matches the requested number of depth bits, falling back on what is supported..." << endl;
        else
          throw vgl_runtime_error("Unable to find a supported Vulkan depth format");
      }

      auto device = instance->getDefaultDevice();
      auto pool = instance->getTransferCommandPool();

      depthAttachment = new VulkanTexture(device, VulkanTexture::TT_2D, pool, graphicsQueue);
      depthAttachment->initImage(swapChainExtent.width, swapChainExtent.height, 1, depthFormat, VulkanTexture::U_DEPTH_STENCIL_ATTACHMENT);

      if(initParams.multiSamples > 1)
      {
        //the jury is out on whether or not I really would need 1 of these for each swapchain image
        msaaDepthTarget = new VulkanTexture(device, VulkanTexture::TT_2D, pool, graphicsQueue);
        msaaDepthTarget->initImage(swapChainExtent.width, swapChainExtent.height, 1, depthFormat, VulkanTexture::U_DEPTH_STENCIL_ATTACHMENT, 0, initParams.multiSamples);
      }
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
          throw vgl_runtime_error("Failed to create Vulkan swapchain semaphores!");
        }
        frameIds[i] = 0;
      }
    }

    void VulkanSwapChainBase::completeFrame(uint64_t frameId)
    {
      if(frameId > completedFrameId)
      {
        completedFrameId = frameId;
        instance->getResourceMonitor()->setCompletedFrame(completedFrameId);
      }
    }
  }
}