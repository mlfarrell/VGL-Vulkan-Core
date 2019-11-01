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

#ifndef VGL_VULKAN_CORE_STANDALONE
#define VGL_VULKAN_USE_SPIRV_CROSS
#define VGL_VULKAN_CORE_USE_VMA
#endif

#include "VulkanShaderProgram.h"
#include "VulkanTexture.h"
#include "VulkanBufferGroup.h"
#include "VulkanVertexArray.h"
#include "VulkanFrameBuffer.h"

#ifdef DEBUG
#define VGL_CORE_PERF_WARNING_DEBUG(failCondition, message) \
{                                                           \
  static bool warned = false;                               \
  if((failCondition) && !warned)                              \
  {                                                         \
    verr << "VGL Performance Warning:  " << message << endl;\
    warned = true;                                          \
  }                                                         \
}
#else
#define VGL_CORE_PERF_WARNING_DEBUG(...)
#endif
