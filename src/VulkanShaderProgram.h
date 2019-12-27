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
#include <string>
#include <vector>
#include "SequentialIdentifier.h"
#include "VecTypes.h"
#include "MatTypes.h"

namespace vgl
{
  namespace core
  {
    class VulkanPipelineStateCache;
    class VulkanBufferGroup;
    class VulkanPipeline;
    class VulkanFrameBuffer;
    struct VulkanPipelineState;

    class VulkanShaderProgram : public SequentialIdentifier
    {
    public:
      enum ShaderType { ST_VERTEX, ST_FRAGMENT, ST_GEOMETRY, ST_COMPUTE };

      VulkanShaderProgram();
      VulkanShaderProgram(VkDevice device);
      ~VulkanShaderProgram();

      ///This must be called before any of the high-level uniform methods are used
      void enableIntrospectionGLSL(bool enabled);

      bool addShaderSPIRV(ShaderType type, const std::string &spirvPath);
      bool addShaderSPIRV(ShaderType type, const uint8_t *spirData, size_t n);

      bool addShaderGLSL(ShaderType type, const std::string &glslSource);
      bool linkShadersGLSL();

      inline VkShaderModule getVertexShader() { return vertexShader; }
      inline VkShaderModule getFragmentShader() { return fragmentShader; }
      inline VkShaderModule getGeometryShader() { return geometryShader; }
      inline VkShaderModule getComputeShader() { return computeShader; }

      inline VkPipelineLayout getPipelineLayout() { return pipelineLayout; }
      inline void setPipelineLayout(VkPipelineLayout layout) { pipelineLayout = layout; }

      void createPipelineStateCache();
      VulkanPipeline *pipelineForState(const VulkanPipelineState &state, VulkanFrameBuffer *renderTarget);

      inline const std::string &getShaderCompilationLogs() { return shaderCompilationLogs; }
      inline const std::string &getShaderLinkLogs() { return shaderLinkLogs; }

      //Uniform Buffer setting (low level)
      ///The indexing into this buffer group will be uboIndex*numSwapchainImages + swapchainImageIndex
      void setDynamicUBOs(VulkanBufferGroup *ubos, int numSwapchainImages);
      inline VulkanBufferGroup *getDynamicUbos() { return dynamicUbos; }
      inline bool hasDynamicUBOs() { return (dynamicUbos != nullptr); }
      bool updateDynamicUboState(int imageIndex, int uboIndex, int destOffset, void *uboState, int len, uint32_t &outUboOffset);

      ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      //Individual uniform variable setting (high level, very inefficient)
      //The host memory needed for this old-school shader uniform setting is externally managed.
      //Thus, for these setShaderUniform* methods, the data is stored back into the host pointer
      //provided by setShaderUniformHostPtr().  No actual uniform buffer data is updated until
      //updateDynamicUboState() is called
      int32_t getUniformLocation(const std::string &str);
      void setShaderUniform(int32_t location, const float4 &val);
      void setShaderUniform(int32_t location, const float3 &val);
      void setShaderUniform(int32_t location, const float2 &val);
      void setShaderUniform(int32_t location, const float &val);
      void setShaderUniform(int32_t location, const int4 &val);
      void setShaderUniform(int32_t location, const int3 &val);
      void setShaderUniform(int32_t location, const int2 &val);
      void setShaderUniform(int32_t location, const int &val);

      void setShaderUniform(int32_t location, const mat2 &mat2);
      void setShaderUniform(int32_t location, const mat3 &val);
      void setShaderUniform(int32_t location, const mat4 &val);

      ///Must be set before setShaderUniform() methods are called
      void setShaderUniformHostPtr(void *uniformHostBuffer);
      ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

      ///These don't do anything in vulkan, and are provided for compat with OpenGL code
      inline void bindAttributeLocation(uint32_t location, const std::string &name) { }
      inline void bindFragmentOutputLocation(uint32_t location, const std::string &name) { }
      inline void use() {}

      struct ShaderRef
      {
        uint32_t unused;
        operator bool() const { return false; }
        void operator =(int) {}
      };

      inline ShaderRef createShader(ShaderType type) { return {}; }
      inline void attachShader(const ShaderRef &sr) { }
      inline void detachAndDestroyShader(const ShaderRef &sr) { }

      struct UniformBufferMemberInfo
      {
        int offset, size;
        std::string name;
        uint32_t type;
        int arrayIndex;
      };

      std::vector<UniformBufferMemberInfo> getUniformBufferMemberInfos(uint32_t set, uint32_t binding);

      uint64_t lastDynamicUboFrame = 0;
    protected:
      VkDevice device;
      VkShaderModule vertexShader = VK_NULL_HANDLE, fragmentShader = VK_NULL_HANDLE, geometryShader = VK_NULL_HANDLE,
                     computeShader = VK_NULL_HANDLE;
      std::string shaderCompilationLogs, shaderLinkLogs;

      //These are only utilized if introspectionEnabledGLSL is set to true
      bool introspectionEnabledGLSL = false;
      std::string vertexShaderAssembly, fragmentShaderAsssembly, geometryShaderAssembly, computeShaderAssembly;
      std::vector<uint32_t> vertexShaderBin, fragmentShaderBin, geometryShaderBin, computeShaderBin;
      std::vector<UniformBufferMemberInfo> currentUniformMemberInfos;
      void *uniformHostBufferPtr = nullptr;

      VulkanPipelineStateCache *pipelineStateCache = nullptr;

      VulkanBufferGroup *dynamicUbos = nullptr;
      VkDeviceSize minUniformBufferOffsetAlignment = 0;
      VkDeviceSize maxUniformBufferRange = 0;
      VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

      struct DynamicUboState
      {
        int size = 0;
      };
      DynamicUboState *dynamicUboStates = nullptr;
      int numDynamicUboStates, numSwapchainImages;
    };

    typedef VulkanShaderProgram ShaderProgram;
  }
}
