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
#include <fstream>
#include <sstream>
#include <regex>

#define VGL_VULKAN_SHADER_USE_SHADERC 
#ifdef VGL_VULKAN_SHADER_USE_SHADERC
#include "shaderc/shaderc.hpp"
#else
#include "glslang/Include/ShHandle.h"
#include "glslang/Include/revision.h"
#include "glslang/Public/ShaderLang.h"
#include "SPIRV/doc.h"
#include "SPIRV/disassemble.h"
#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/GLSL.std.450.h"
#endif
#include "VulkanInstance.h"
#include "VulkanShaderProgram.h"
#include "VulkanBufferGroup.h"
#include "VulkanPipelineStateCache.h"
#include "ShaderUniformTypeEnums.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "System.h"
#include "StateMachine.h"
#endif

#define VGL_VULKAN_SHADER_USE_SHADERC 
#ifndef VGL_ALIGN
#define VGL_ALIGN(x, a) ((x + a - 1) & ~(a - 1))
#endif

using namespace std;

namespace vgl
{
  namespace core
  {
    static vector<VulkanShaderProgram::UniformBufferMemberInfo> getShaderAsmStructInfos(const string &asmSrc, const string &op);
    static vector<VulkanShaderProgram::UniformBufferMemberInfo> getShaderAsmArrayInfos(const string &asmSrc, const string &op);
    static bool updateVarInfoFromTypeName(const string &asmSrc, const string &varTypeName, int &index, vector<VulkanShaderProgram::UniformBufferMemberInfo> &allInfos);

    VulkanShaderProgram::VulkanShaderProgram()
      : VulkanShaderProgram(VK_NULL_HANDLE)
    {
      device = VulkanInstance::currentInstance().getDefaultDevice();

#ifndef VGL_VULKAN_CORE_STANDALONE
      auto csm = vgl::StateMachine::machine().getCoreStateMachine();
      pipelineLayout = csm->getCommonPLLayout1();
#endif
    }

    VulkanShaderProgram::VulkanShaderProgram(VkDevice device)
      : device(device)
    {
      if(!device)
        device = VulkanInstance::currentInstance().getDefaultDevice();
      minUniformBufferOffsetAlignment = VulkanInstance::currentInstance().getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;

#ifndef VGL_VULKAN_CORE_STANDALONE
      auto csm = vgl::StateMachine::machine().getCoreStateMachine();
      pipelineLayout = csm->getCommonPLLayout1();
#endif
    }

    VulkanShaderProgram::~VulkanShaderProgram()
    {
      if(vertexShader)
        vkDestroyShaderModule(device, vertexShader, nullptr);
      if(fragmentShader)
        vkDestroyShaderModule(device, fragmentShader, nullptr);
      if(geometryShader)
        vkDestroyShaderModule(device, geometryShader, nullptr);
      if(pipelineStateCache)
        delete pipelineStateCache;
      if(dynamicUboStates)
      {
        delete []dynamicUboStates;
      }
    }

    void VulkanShaderProgram::enableIntrospectionGLSL(bool enabled)
    {
      introspectionEnabledGLSL = enabled;
    }

    bool VulkanShaderProgram::addShaderSPIRV(ShaderType type, const string &spirvPath)
    {
      ifstream file(spirvPath, ios::ate | ios::binary);

      if(!file.is_open())
        return false;

      size_t fileSize = (size_t)file.tellg();
      uint8_t *data = new uint8_t[fileSize];

      file.seekg(0);
      file.read((char *)data, fileSize);

      bool result = addShaderSPIRV(type, data, fileSize);
      delete []data;

      file.close();
      return result;
    }

    bool VulkanShaderProgram::addShaderSPIRV(ShaderType type, const uint8_t *spirData, size_t n)
    {
      VkShaderModule *target = nullptr;

      switch(type)
      {
        case ST_VERTEX: target = &vertexShader; break;
        case ST_FRAGMENT: target = &fragmentShader; break;
        case ST_GEOMETRY: target = &geometryShader; break;
        default: return false; break;
      }

      if(*target)
        vkDestroyShaderModule(device, *target, nullptr);

      VkShaderModuleCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      createInfo.codeSize = n;
      createInfo.pCode = (const uint32_t *)spirData;
      currentUniformMemberInfos.clear();

      if(vkCreateShaderModule(device, &createInfo, nullptr, target) != VK_SUCCESS)
        return false;

      //this invalidates the shader pipeline cache
      if(pipelineStateCache)
      {
        delete pipelineStateCache;
        pipelineStateCache = nullptr;
      }

      return true;
    }

#ifdef VGL_VULKAN_SHADER_USE_SHADERC
    bool VulkanShaderProgram::addShaderGLSL(ShaderType type, const string &glslSource)
    {
      auto preprocess = [this](const string &source_name, shaderc_shader_kind kind, const string &source) -> string {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        //options.AddMacroDefinition("MY_DEFINE", "1");

        shaderc::PreprocessedSourceCompilationResult result =
          compiler.PreprocessGlsl(source, kind, source_name.c_str(), options);

        if(result.GetCompilationStatus() != shaderc_compilation_status_success) {
          shaderCompilationLogs += result.GetErrorMessage();
          return "";
        }

        return { result.cbegin(), result.cend() };
      };

      auto compile = [this](const string &source_name, shaderc_shader_kind kind, const string &source, bool optimize) -> vector<uint32_t> {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        //options.AddMacroDefinition("MY_DEFINE", "1");
        if(optimize) 
          options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, kind, source_name.c_str(), options);

        if(module.GetCompilationStatus() != shaderc_compilation_status_success) 
        {
          shaderCompilationLogs += module.GetErrorMessage();
          return vector<uint32_t>();
        }

        if(introspectionEnabledGLSL)
        {
          //intentionally not optimizing this step (asm only used for introspection)
          auto moduleAsm = compiler.CompileGlslToSpvAssembly(source, kind, source_name.c_str(), shaderc::CompileOptions());

          if(moduleAsm.GetCompilationStatus() == shaderc_compilation_status_success)
          {
            if(kind == shaderc_glsl_vertex_shader)
              vertexShaderAssembly = string(moduleAsm.begin(), moduleAsm.end());
            else if(kind == shaderc_glsl_fragment_shader)
              fragmentShaderAsssembly = string(moduleAsm.begin(), moduleAsm.end());
            else if(kind == shaderc_glsl_geometry_shader)
              geometryShaderAssembly = string(moduleAsm.begin(), moduleAsm.end());
          }
        }

        return { module.cbegin(), module.cend() };
      };

      auto shaderType = [=] {
        switch(type)
        {
          case ST_VERTEX: return shaderc_glsl_vertex_shader; break;
          case ST_FRAGMENT: return shaderc_glsl_fragment_shader; break;
          case ST_GEOMETRY: return shaderc_glsl_geometry_shader; break;
        }

        return shaderc_glsl_vertex_shader;
      };

#ifdef DEBUG
      static const bool optimize = false;
#else
      static const bool optimize = true;
#endif

      auto spirv = compile("unnamed shader", shaderType(), glslSource, optimize);
      if(!spirv.empty())
      {
        bool result = addShaderSPIRV(type, (const uint8_t *)spirv.data(), spirv.size()*sizeof(uint32_t));
        return result;
      }

      return false;
    }

    bool VulkanShaderProgram::linkShadersGLSL()
    {
      //not sure what to do here yet to validate this
      return true;
    }

#else

    //Glslang based compiler below
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
    // Copyright (C) 2013-2016 LunarG, Inc.
    //
    // All rights reserved.
    //
    // Redistribution and use in source and binary forms, with or without
    // modification, are permitted provided that the following conditions
    // are met:
    //
    //    Redistributions of source code must retain the above copyright
    //    notice, this list of conditions and the following disclaimer.
    //
    //    Redistributions in binary form must reproduce the above
    //    copyright notice, this list of conditions and the following
    //    disclaimer in the documentation and/or other materials provided
    //    with the distribution.
    //
    //    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
    //    contributors may be used to endorse or promote products derived
    //    from this software without specific prior written permission.
    //
    // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    // "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    // LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    // FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    // COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    // INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    // BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    // LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    // CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    // LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    // ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    // POSSIBILITY OF SUCH DAMAGE.
    //

    //glslang is not a good library...
    enum TOptions {
      EOptionNone = 0,
      EOptionIntermediate = (1 <<  0),
      EOptionSuppressInfolog = (1 <<  1),
      EOptionMemoryLeakMode = (1 <<  2),
      EOptionRelaxedErrors = (1 <<  3),
      EOptionGiveWarnings = (1 <<  4),
      EOptionLinkProgram = (1 <<  5),
      EOptionMultiThreaded = (1 <<  6),
      EOptionDumpConfig = (1 <<  7),
      EOptionDumpReflection = (1 <<  8),
      EOptionSuppressWarnings = (1 <<  9),
      EOptionDumpVersions = (1 << 10),
      EOptionSpv = (1 << 11),
      EOptionHumanReadableSpv = (1 << 12),
      EOptionVulkanRules = (1 << 13),
      EOptionDefaultDesktop = (1 << 14),
      EOptionOutputPreprocessed = (1 << 15),
      EOptionOutputHexadecimal = (1 << 16),
      EOptionReadHlsl = (1 << 17),
      EOptionCascadingErrors = (1 << 18),
      EOptionAutoMapBindings = (1 << 19),
      EOptionFlattenUniformArrays = (1 << 20),
      EOptionNoStorageFormat = (1 << 21),
      EOptionKeepUncalled = (1 << 22),
      EOptionHlslOffsets = (1 << 23),
      EOptionHlslIoMapping = (1 << 24),
      EOptionAutoMapLocations = (1 << 25),
      EOptionDebug = (1 << 26),
      EOptionStdin = (1 << 27),
      EOptionOptimizeDisable = (1 << 28),
      EOptionOptimizeSize = (1 << 29),
      EOptionInvertY = (1 << 30),
      EOptionDumpBareVersion = (1 << 31),
    };

    const TBuiltInResource DefaultTBuiltInResource = {
      /* .MaxLights = */ 32,
      /* .MaxClipPlanes = */ 6,
      /* .MaxTextureUnits = */ 32,
      /* .MaxTextureCoords = */ 32,
      /* .MaxVertexAttribs = */ 64,
      /* .MaxVertexUniformComponents = */ 4096,
      /* .MaxVaryingFloats = */ 64,
      /* .MaxVertexTextureImageUnits = */ 32,
      /* .MaxCombinedTextureImageUnits = */ 80,
      /* .MaxTextureImageUnits = */ 32,
      /* .MaxFragmentUniformComponents = */ 4096,
      /* .MaxDrawBuffers = */ 32,
      /* .MaxVertexUniformVectors = */ 128,
      /* .MaxVaryingVectors = */ 8,
      /* .MaxFragmentUniformVectors = */ 16,
      /* .MaxVertexOutputVectors = */ 16,
      /* .MaxFragmentInputVectors = */ 15,
      /* .MinProgramTexelOffset = */ -8,
      /* .MaxProgramTexelOffset = */ 7,
      /* .MaxClipDistances = */ 8,
      /* .MaxComputeWorkGroupCountX = */ 65535,
      /* .MaxComputeWorkGroupCountY = */ 65535,
      /* .MaxComputeWorkGroupCountZ = */ 65535,
      /* .MaxComputeWorkGroupSizeX = */ 1024,
      /* .MaxComputeWorkGroupSizeY = */ 1024,
      /* .MaxComputeWorkGroupSizeZ = */ 64,
      /* .MaxComputeUniformComponents = */ 1024,
      /* .MaxComputeTextureImageUnits = */ 16,
      /* .MaxComputeImageUniforms = */ 8,
      /* .MaxComputeAtomicCounters = */ 8,
      /* .MaxComputeAtomicCounterBuffers = */ 1,
      /* .MaxVaryingComponents = */ 60,
      /* .MaxVertexOutputComponents = */ 64,
      /* .MaxGeometryInputComponents = */ 64,
      /* .MaxGeometryOutputComponents = */ 128,
      /* .MaxFragmentInputComponents = */ 128,
      /* .MaxImageUnits = */ 8,
      /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
      /* .MaxCombinedShaderOutputResources = */ 8,
      /* .MaxImageSamples = */ 0,
      /* .MaxVertexImageUniforms = */ 0,
      /* .MaxTessControlImageUniforms = */ 0,
      /* .MaxTessEvaluationImageUniforms = */ 0,
      /* .MaxGeometryImageUniforms = */ 0,
      /* .MaxFragmentImageUniforms = */ 8,
      /* .MaxCombinedImageUniforms = */ 8,
      /* .MaxGeometryTextureImageUnits = */ 16,
      /* .MaxGeometryOutputVertices = */ 256,
      /* .MaxGeometryTotalOutputComponents = */ 1024,
      /* .MaxGeometryUniformComponents = */ 1024,
      /* .MaxGeometryVaryingComponents = */ 64,
      /* .MaxTessControlInputComponents = */ 128,
      /* .MaxTessControlOutputComponents = */ 128,
      /* .MaxTessControlTextureImageUnits = */ 16,
      /* .MaxTessControlUniformComponents = */ 1024,
      /* .MaxTessControlTotalOutputComponents = */ 4096,
      /* .MaxTessEvaluationInputComponents = */ 128,
      /* .MaxTessEvaluationOutputComponents = */ 128,
      /* .MaxTessEvaluationTextureImageUnits = */ 16,
      /* .MaxTessEvaluationUniformComponents = */ 1024,
      /* .MaxTessPatchComponents = */ 120,
      /* .MaxPatchVertices = */ 32,
      /* .MaxTessGenLevel = */ 64,
      /* .MaxViewports = */ 16,
      /* .MaxVertexAtomicCounters = */ 0,
      /* .MaxTessControlAtomicCounters = */ 0,
      /* .MaxTessEvaluationAtomicCounters = */ 0,
      /* .MaxGeometryAtomicCounters = */ 0,
      /* .MaxFragmentAtomicCounters = */ 8,
      /* .MaxCombinedAtomicCounters = */ 8,
      /* .MaxAtomicCounterBindings = */ 1,
      /* .MaxVertexAtomicCounterBuffers = */ 0,
      /* .MaxTessControlAtomicCounterBuffers = */ 0,
      /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
      /* .MaxGeometryAtomicCounterBuffers = */ 0,
      /* .MaxFragmentAtomicCounterBuffers = */ 1,
      /* .MaxCombinedAtomicCounterBuffers = */ 1,
      /* .MaxAtomicCounterBufferSize = */ 16384,
      /* .MaxTransformFeedbackBuffers = */ 4,
      /* .MaxTransformFeedbackInterleavedComponents = */ 64,
      /* .MaxCullDistances = */ 8,
      /* .MaxCombinedClipAndCullDistances = */ 8,
      /* .MaxSamples = */ 4,
      /* .limits = */{
      /* .nonInductiveForLoops = */ 1,
      /* .whileLoops = */ 1,
      /* .doWhileLoops = */ 1,
      /* .generalUniformIndexing = */ 1,
      /* .generalAttributeMatrixVectorIndexing = */ 1,
      /* .generalVaryingIndexing = */ 1,
      /* .generalSamplerIndexing = */ 1,
      /* .generalVariableIndexing = */ 1,
      /* .generalConstantMatrixVectorIndexing = */ 1,
    } };


    bool VulkanShaderProgram::addShaderGLSL(ShaderType type, const string &glslSource)
    {
      static bool init = false;
      const char *entryPointName = nullptr;
      const char *sourceEntryPointName = nullptr;

      if(!init)
      {
        glslang::InitializeProcess();
        init = true;
      }

      struct CompUnit
      {
        string source;
        EShLanguage stage;
      };

      auto vulkanClientVersion = glslang::EShTargetVulkan_1_0;
      auto targetVersion = glslang::EShTargetSpv_1_0;
      const bool targetHlslFunctionality1 = false;

      int options = 0;
      options |= EOptionSpv;
      options |= EOptionVulkanRules;
      options |= EOptionLinkProgram;
      //options |= EOptionOutputPreprocessed; //not sure on this one

      TBuiltInResource resources;
      resources = DefaultTBuiltInResource;

      auto compileAndLinkShaderUnits = [=](vector<CompUnit> compUnits) {
        vector<uint32_t> spirv;
        bool compileFailed = false;
        bool linkFailed = false;
        list<glslang::TShader *> shaders;
        EShMessages messages = EShMsgDefault;
        messages = (EShMessages)(messages | EShMsgSpvRules);
        messages = (EShMessages)(messages | EShMsgVulkanRules);

        auto strings = new const char *[compUnits.size()];

        //
        // Per-shader processing...
        //
        glslang::TProgram *programPtr = new glslang::TProgram;
        auto &program = *programPtr;
        int ci = 0;
        for(auto it = compUnits.cbegin(); it != compUnits.cend(); it++,ci++) 
        {
          const auto &compUnit = *it;
          glslang::TShader *shader = new glslang::TShader(compUnit.stage);

          strings[ci] = compUnit.source.c_str();
          shader->setStrings(strings, 1);
          if(entryPointName) // HLSL todo: this needs to be tracked per compUnits
            shader->setEntryPoint(entryPointName);
          if(sourceEntryPointName) 
          {
            if(entryPointName == nullptr)
            {
              printf("Warning: Changing source entry point name without setting an entry-point name.\n");
            }
            shader->setSourceEntryPoint(sourceEntryPointName);
          }

          //if(UserPreamble.isSet())
          //  shader->setPreamble(UserPreamble.get());
          //shader->addProcesses(Processes);

          shader->setFlattenUniformArrays((options & EOptionFlattenUniformArrays) != 0);
          shader->setNoStorageFormat((options & EOptionNoStorageFormat) != 0);

          if(options & EOptionHlslIoMapping)
            shader->setHlslIoMapping(true);

          if(options & EOptionAutoMapBindings)
            shader->setAutoMapBindings(true);

          if(options & EOptionAutoMapLocations)
            shader->setAutoMapLocations(true);

          if(options & EOptionInvertY)
            shader->setInvertY(true);

          // Set up the environment, some subsettings take precedence over earlier
          // ways of setting things.
          if(options & EOptionSpv) 
          {
            if(options & EOptionVulkanRules) 
            {
              const int clientInputSemanticsVersion = 100;
              shader->setEnvInput((options & EOptionReadHlsl) ? glslang::EShSourceHlsl
                : glslang::EShSourceGlsl,
                compUnit.stage, glslang::EShClientVulkan, clientInputSemanticsVersion);
              shader->setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
            }
            else 
            {
              /*shader->setEnvInput((options & EOptionReadHlsl) ? glslang::EShSourceHlsl
                : glslang::EShSourceGlsl,
                compUnit.stage, glslang::EShClientOpenGL, ClientInputSemanticsVersion);
              shader->setEnvClient(glslang::EShClientOpenGL, OpenGLClientVersion);*/
            }
            shader->setEnvTarget(glslang::EShTargetSpv, targetVersion);

            if(targetHlslFunctionality1)
              shader->setEnvTargetHlslFunctionality1();
          }

          shaders.push_back(shader);

          const int defaultVersion = options & EOptionDefaultDesktop ? 110 : 100;
          //glslang::TShader::Includer includer;

          if(options & EOptionOutputPreprocessed) 
          {
            /*string str;
            if(shader->preprocess(&resources, defaultVersion, ENoProfile, false, false,
              messages, &str, includer)) 
            {
              PutsIfNonEmpty(str.c_str());
            }
            else 
            {
              compileFailed = true;
            }
            StderrIfNonEmpty(shader->getInfoLog());
            StderrIfNonEmpty(shader->getInfoDebugLog());
            continue;*/
          }
          if(!shader->parse(&resources, defaultVersion, false, messages))
            compileFailed = true;

          program.addShader(shader);

          if(!(options & EOptionSuppressInfolog) &&
             !(options & EOptionMemoryLeakMode)) 
          {
            shaderCompilationLogs += shader->getInfoLog();
            shaderCompilationLogs += shader->getInfoDebugLog();
          }
        }

        //
        // Program-level processing...
        //

        // Link
        if(!(options & EOptionOutputPreprocessed) && !program.link(messages))
          linkFailed = true;

        // Map IO
        if(options & EOptionSpv) 
        {
          if(!program.mapIO())
            linkFailed = true;
        }

        // Report
        if(!(options & EOptionSuppressInfolog) &&
           !(options & EOptionMemoryLeakMode)) 
        {
          shaderCompilationLogs += program.getInfoLog();
          shaderCompilationLogs += program.getInfoDebugLog();
        }

        // Reflect
        if(options & EOptionDumpReflection) 
        {
          program.buildReflection();
          program.dumpReflection();
        }

        // Dump SPIR-V
        if(options & EOptionSpv) 
        {
          if(!(compileFailed || linkFailed)) 
          {
            for(int stage = 0; stage < EShLangCount; ++stage) 
            {
              if(program.getIntermediate((EShLanguage)stage)) 
              {
                string warningsErrors;
                spv::SpvBuildLogger logger;
                glslang::SpvOptions spvOptions;

                if(options & EOptionDebug)
                  spvOptions.generateDebugInfo = true;
                spvOptions.disableOptimizer = (options & EOptionOptimizeDisable) != 0;
                spvOptions.optimizeSize = (options & EOptionOptimizeSize) != 0;
                glslang::GlslangToSpv(*program.getIntermediate((EShLanguage)stage), spirv, &logger, &spvOptions);

                // Dump the spv to a file or stdout, etc., but only if not doing
                // memory/perf testing, as it's not internal to programmatic use.
                if(!(options & EOptionMemoryLeakMode)) 
                {
                  shaderCompilationLogs += logger.getAllMessages();                  

                  if(options & EOptionHumanReadableSpv) 
                  {
                    spv::Disassemble(cout, spirv);
                  }
                }
              }
            }
          }
        }

        // Free everything up, program has to go before the shaders
        // because it might have merged stuff from the shaders, and
        // the stuff from the shaders has to have its destructors called
        // before the pools holding the memory in the shaders is freed.
        delete []strings;
        delete programPtr;
        while(shaders.size() > 0) 
        {
          delete shaders.back();
          shaders.pop_back();
        }

        return spirv;
      };      

      auto shaderType = [=] {
        switch(type)
        {
          case ST_VERTEX: return EShLangVertex; break;
          case ST_FRAGMENT: return EShLangFragment; break;
          case ST_GEOMETRY: return EShLangGeometry; break;
        }

        return EShLangVertex;
      };


      vector<CompUnit> shaderPair;
      shaderPair.push_back({ glslSource, shaderType() });

      auto spirv = compileAndLinkShaderUnits(shaderPair);

      if(!spirv.empty())
      {
        bool result = addShaderSPIRV(type, (const uint8_t *)spirv.data(), spirv.size()*sizeof(uint32_t));
        return result;
      }
      else
      {
        cerr << shaderCompilationLogs << endl;
      }

      return false;
    }

#endif

    void VulkanShaderProgram::setDynamicUBOs(VulkanBufferGroup *ubos, int numImages)
    {
      dynamicUbos = ubos;
      numDynamicUboStates = ubos->getNumBuffers();
      numSwapchainImages = numImages;
      if(dynamicUboStates)
        delete []dynamicUboStates;
      dynamicUboStates = new DynamicUboState[numDynamicUboStates];
    }

    bool VulkanShaderProgram::updateDynamicUboState(int imageIndex, int uboIndex, int offset, void *uboState, int len, uint32_t &uboOffset)
    {
      int index = uboIndex*numSwapchainImages + imageIndex;
      DynamicUboState &dynamicState = dynamicUboStates[index];

      //if(dynamicUbos->isDeviceLocal())
      //  throw vgl_runtime_error("Cannot call updateDynamicUboState with device local uniform buffers (you should manually populate buffer in advance instead)");

      if(!dynamicState.size)
      {
        if(offset != 0)
          throw vgl_runtime_error("Cannot call updateDynamicUboState with nonzero offset for initial update");

        dynamicState.size = len;
        if(minUniformBufferOffsetAlignment > 0)
          dynamicState.size = (int)VGL_ALIGN(len, minUniformBufferOffsetAlignment);
      }

#ifdef DEBUG
      int alignedLen = (int)((minUniformBufferOffsetAlignment > 0) ? VGL_ALIGN(len, minUniformBufferOffsetAlignment) : len);
      if(alignedLen != dynamicState.size)
        throw vgl_runtime_error("updateDynamicUboState size mismatch");
#endif

      //make sure we haven't ran off the end of the dynamic UBO
      if(uboOffset+dynamicState.size >= dynamicUbos->getSize(index))
        return false;

      //TODO: sub-updates?
      uint8_t *uboHostData = (uint8_t *)dynamicUbos->getPersistentlyMappedAddress(index);
      memcpy(uboHostData+uboOffset, uboState, len);

      uboOffset += dynamicState.size;
      return true;
    }

    void VulkanShaderProgram::createPipelineStateCache()
    {
      if(!pipelineStateCache)
        pipelineStateCache = new VulkanPipelineStateCache(device, this);
    }

    VulkanPipeline *VulkanShaderProgram::pipelineForState(const VulkanPipelineState &state, VulkanFrameBuffer *renderTarget)
    {
      if(!pipelineStateCache)
        pipelineStateCache = new VulkanPipelineStateCache(device, this);

      return pipelineStateCache->getCachedPipeline(state, renderTarget);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    #define CHECK_INTROSPECTION() if(!introspectionEnabledGLSL)  \
      throw vgl_runtime_error("Can't call VulkanShaderProgram::setShaderUniform() without enabling shader introspection!")
    #define CHECK_HOST_BUFFER() if(!uniformHostBufferPtr) \
      throw vgl_runtime_error("You must first call setShaderUniformHostPtr() with a valid host buffer before calling VulkanShaderProgram::setShaderUniform()")

    int32_t VulkanShaderProgram::getUniformLocation(const string &str) 
    {
      CHECK_INTROSPECTION();

      if(currentUniformMemberInfos.empty())
        getUniformBufferMemberInfos(0, 0);

      //we can go to a map for this if needed down the road
      int32_t index = 0;
      for(auto &info : currentUniformMemberInfos)
      {
        if(info.name == str)
        {
          return index;
        }

        index++;
      }

      return -1;
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const float4 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const float3 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const float2 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const float &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const int4 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const int3 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const int2 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const int &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const mat2 &mat2)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(mat2)>::type>::type *)ptr) = mat2;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const mat3 &matrix)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        float4 matrix3[3];

        matrix3[0] = { matrix.m00, matrix.m01, matrix.m02, 0 };
        matrix3[1] = { matrix.m10, matrix.m11, matrix.m12, 0 };
        matrix3[2] = { matrix.m20, matrix.m21, matrix.m22, 0 };

        memcpy(ptr, matrix3, sizeof(matrix3));
        //*((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniform(int32_t location, const mat4 &val)
    {
      CHECK_INTROSPECTION();
      CHECK_HOST_BUFFER();

      if(location >= 0 && location < (int)currentUniformMemberInfos.size())
      {
        uint8_t *ptr = (uint8_t *)uniformHostBufferPtr+currentUniformMemberInfos[location].offset;
        *((remove_const<remove_reference<decltype(val)>::type>::type *)ptr) = val;
      }
    }

    void VulkanShaderProgram::setShaderUniformHostPtr(void *uniformHostBuffer)
    {
      uniformHostBufferPtr = uniformHostBuffer;
    }

    static vector<vector<string>> findPatternMatches(const string &str, const string &patt)
    {
      regex pattern(patt);
      auto matchesBegin = sregex_iterator(str.begin(), str.end(), pattern);
      auto matchesEnd = sregex_iterator();
      vector<vector<string>> ret;

      for(auto it = matchesBegin; it != matchesEnd; it++)
      {
        std::smatch m = *it;
        if(m.size() == 2)
          ret.push_back({ m[1] });
        else if(m.size() >= 3)
          ret.push_back(vector<string>(++m.begin(), m.end()));
      }

      return ret;
    };

    static bool findPatternMatch(string &str, const string &patt, string mustMatch) 
    {
      regex pattern(patt);
      auto matchesBegin = sregex_iterator(str.begin(), str.end(), pattern);
      auto matchesEnd = sregex_iterator();
      vector<string> ret;

      for(auto it = matchesBegin; it != matchesEnd; it++)
      {
        std::smatch m = *it;
        if(m.size() > 1 && m[1] == mustMatch)
          return true;
      }

      return false;
    };

    static bool updateVarInfoFromTypeName(const string &asmSrc, const string &varTypeName, int &index, vector<VulkanShaderProgram::UniformBufferMemberInfo> &allInfos)
    {
      VulkanShaderProgram::UniformBufferMemberInfo &info = allInfos[index];

      //I've hit my limit for what I'm willing to parse here, if we ever go with a different compiler frontend
      //we may need to resolve these names to be certain of the type
      if(varTypeName == "%float")
      {
        info.type = UT_FLOAT;
        info.size = 4;
      }
      else if(varTypeName == "%v2float")
      {
        info.type = UT_FLOAT_VEC2;
        info.size = 8;
      }
      else if(varTypeName == "%v3float")
      {
        info.type = UT_FLOAT_VEC3;
        info.size = 16;
      }
      else if(varTypeName == "%v4float")
      {
        info.type = UT_FLOAT_VEC4;
        info.size = 16;
      }

      else if(varTypeName == "%int" || varTypeName == "%uint")
      {
        info.type = UT_INT;
        info.size = 4;
      }
      else if(varTypeName == "%v2int")
      {
        info.type = UT_INT_VEC2;
        info.size = 8;
      }
      else if(varTypeName == "%v3int")
      {
        info.type = UT_INT_VEC3;
        info.size = 16;
      }
      else if(varTypeName == "%v4int")
      {
        info.type = UT_INT_VEC4;
        info.size = 16;
      }

      else if(varTypeName == "%bool")
      {
        info.type = UT_BOOL;
        info.size = 4;
      }
      else if(varTypeName == "%v2bool")
      {
        info.type = UT_BOOL_VEC2;
        info.size = 8;
      }
      else if(varTypeName == "%v3bool")
      {
        info.type = UT_BOOL_VEC3;
        info.size = 16;
      }
      else if(varTypeName == "%v4bool")
      {
        info.type = UT_BOOL_VEC4;
        info.size = 16;
      }

      else if(varTypeName == "%mat2v2float")
      {
        info.type = UT_FLOAT_MAT2;
        info.size = 16;
      }
      else if(varTypeName == "%mat3v3float")
      {
        info.type = UT_FLOAT_MAT3;
        info.size = 48;
      }
      else if(varTypeName == "%mat4v4float")
      {
        info.type = UT_FLOAT_MAT4;
        info.size = 64;
      }
      else
      {
        //most likely another type (struct or array)
        auto structVec = findPatternMatches(asmSrc, varTypeName + " = OpTypeStruct ([^\n]*)");
        if(!structVec.empty())
        {
          //mark the original info name for deletion
          string infoName = info.name;
          int infoOffset = info.offset;
          int infoArrayIndex = info.arrayIndex;
          int insertionPos = index;

          info.type = numeric_limits<uint32_t>::max();
          allInfos.erase(remove_if(allInfos.begin(), allInfos.end(), [](const VulkanShaderProgram::UniformBufferMemberInfo &info) {
            return (info.type == numeric_limits<uint32_t>::max());
          }), allInfos.end());
          index--;

          //..descend into the nested type
          auto subInfos = getShaderAsmStructInfos(asmSrc, varTypeName);

          //..and append it to this one
          for(auto subInfo : subInfos)
          {
            subInfo.name = infoName + "." + subInfo.name;
            subInfo.offset += infoOffset;
            subInfo.arrayIndex = infoArrayIndex;
            allInfos.insert(allInfos.begin()+insertionPos, subInfo);
            index++;
          }
        }
        else
        {
          auto arrVec = findPatternMatches(asmSrc, varTypeName + " = OpTypeArray ([^\n]*)");
          if(!arrVec.empty())
          {
            //mark the original info name for deletion
            string infoName = info.name;
            int infoOffset = info.offset;
            int infoArrayIndex = info.arrayIndex;
            int insertionPos = index;

            info.type = numeric_limits<uint32_t>::max();
            allInfos.erase(remove_if(allInfos.begin(), allInfos.end(), [](const VulkanShaderProgram::UniformBufferMemberInfo &info) {
              return (info.type == numeric_limits<uint32_t>::max());
            }), allInfos.end());
            index--;

            //..descend into the nested type
            auto subInfos = getShaderAsmArrayInfos(asmSrc, varTypeName);

            //..and append it to this one
            for(auto subInfo : subInfos)
            {
              subInfo.name = infoName + '[' + to_string(subInfo.arrayIndex) + ']' + subInfo.name;
              subInfo.offset += infoOffset;
              subInfo.arrayIndex = infoArrayIndex;
              allInfos.insert(allInfos.begin()+insertionPos, subInfo);
              index++;
            }
          }
          else
          {
            return false;
          }
        }
      }

      return true;
    }

    static vector<VulkanShaderProgram::UniformBufferMemberInfo> getShaderAsmStructInfos(const string &asmSrc, const string &op)
    {
      vector<VulkanShaderProgram::UniformBufferMemberInfo> result;

      auto vec = findPatternMatches(asmSrc, "OpMemberName " + op + " ([0-9]+) \"(.*?)\"");
      if(!vec.empty())
      {
        result.resize(vec.size());

        for(auto &uboMatch : vec)
        {
          int index = stoi(uboMatch[0]);
          result[index] = { 0, 0, uboMatch[1], 0, 0 };
        }
      }

      //..and the offsets
      vec = findPatternMatches(asmSrc, "OpMemberDecorate " + op + " ([0-9]+) Offset ([0-9]+)");
      if(!vec.empty())
      {
        for(auto &uboMatch : vec)
        {
          int index = stoi(uboMatch[0]);
          result[index].offset = stoi(uboMatch[1]);
        }
      }

      //.. and the types
      vec = findPatternMatches(asmSrc, op + " = OpTypeStruct ([^\n]*)");
      if(!vec.empty())
      {
        auto varTypeNameList = vec[0][0];
        istringstream istr(varTypeNameList);
        int index = 0;

        while(istr.good())
        {
          string varTypeName;
          istr >> varTypeName;

          if(!istr.fail())
          {
            updateVarInfoFromTypeName(asmSrc, varTypeName, index, result);
          }

          index++;
        }
      }

      return result;
    }

    static vector<VulkanShaderProgram::UniformBufferMemberInfo> getShaderAsmArrayInfos(const string &asmSrc, const string &op)
    {
      vector<VulkanShaderProgram::UniformBufferMemberInfo> result;
      uint32_t stride = 0;

      //..and the offsets
      auto vec = findPatternMatches(asmSrc, "OpDecorate " + op + " ArrayStride ([0-9]+)");
      if(!vec.empty())
      {
        for(auto &uboMatch : vec)
        {
          stride = stoi(uboMatch[0]);
          break;
        }
      }

      //.. and the type
      vec = findPatternMatches(asmSrc, op + " = OpTypeArray ([^\n]*) ([^\n]*)");
      if(stride && !vec.empty())
      {
        auto varTypeName = vec[0][0];
        string arrayCountTypeName = vec[0][1];

        auto arrayCountVec = findPatternMatches(asmSrc, arrayCountTypeName + " = OpConstant ([^\n]*) ([^\n]*)");
        if(!arrayCountVec.empty())
        {
          int arrayCount = stoi(arrayCountVec[0][1]);

          if(arrayCount > 0 && arrayCount < 0xFFFF) //sanity check
          {
            result.resize(arrayCount);

            int i = 0, ai = 0;
            for(ai = 0; ai < arrayCount; i++,ai++)
            {
              result[i].name = "";
              result[i].arrayIndex = ai;
              result[i].offset = stride*ai;

              updateVarInfoFromTypeName(asmSrc, varTypeName, i, result);
            }
          }
        }
      }

      return result;
    }

    vector<VulkanShaderProgram::UniformBufferMemberInfo> VulkanShaderProgram::getUniformBufferMemberInfos(uint32_t set, uint32_t binding) 
    {
      CHECK_INTROSPECTION();
      vector<UniformBufferMemberInfo> result;

      if(set == 0 && binding == 0 && !currentUniformMemberInfos.empty())
        return currentUniformMemberInfos;

      string uboOp;

      auto explicify = [](const string &str) { //explicifyyyyy
        string bracketed;

        for(auto c : str)
        {
          if(c == '%')
            bracketed += (string)"[" + c + "]";
          else
            bracketed += c;
        }
        return bracketed;
      };

      //find by set & binding numbers
      string setPattern = (string)"OpDecorate ([^\n]*?) DescriptorSet " + to_string(set);
      string bindingPattern = (string)"OpDecorate ([^\n]*?) Binding " + to_string(binding);
      auto setMatches = findPatternMatches(vertexShaderAssembly, setPattern);
      for(auto &sm : setMatches)
      {
        auto bm = findPatternMatch(vertexShaderAssembly, bindingPattern, sm[0]);
        if(bm)
        {
          uboOp = sm[0]; //%_
          break;
        }
      }

      if(!uboOp.empty())
      {
        uboOp = explicify(uboOp);

        auto vec = findPatternMatches(vertexShaderAssembly, uboOp + " = OpVariable ([^ ]*)");
        if(!vec.empty())
        {
          auto uniformVarTypeName = explicify(vec[0][0]);
          vec = findPatternMatches(vertexShaderAssembly, uniformVarTypeName + " = OpTypePointer Uniform ([^\n]*)");
          if(!vec.empty())
          {
            auto uniformBlockName = explicify(vec[0][0]);

            //we can now FINALLY get the UBO block member names (the uniforms)
            vec = findPatternMatches(vertexShaderAssembly, "OpMemberName " + uniformBlockName + " ([0-9]+) \"(.*?)\"");
            if(!vec.empty())
            {
              result.resize(vec.size());

              for(auto &uboMatch : vec)
              {
                int index = stoi(uboMatch[0]);
                result[index] = { 0, 0, uboMatch[1], 0 };
              }
            }

            //..and the offsets
            vec = findPatternMatches(vertexShaderAssembly, "OpMemberDecorate " + uniformBlockName + " ([0-9]+) Offset ([0-9]+)");
            if(!vec.empty())
            {
              for(auto &uboMatch : vec)
              {
                int index = stoi(uboMatch[0]);
                result[index].offset = stoi(uboMatch[1]);
              }
            }

            //.. and the types
            vec = findPatternMatches(vertexShaderAssembly, uniformBlockName + " = OpTypeStruct ([^\n]*)");
            if(!vec.empty())
            {
              auto varTypeNameList = vec[0][0];
              istringstream istr(varTypeNameList);
              int index = 0;

              while(istr.good())
              {
                string varTypeName;
                istr >> varTypeName;

                if(!istr.fail())
                {
                  updateVarInfoFromTypeName(vertexShaderAssembly, varTypeName, index, result);
                }

                index++;
              }
            }
          }
        }
      }

      //lastly, look for uniform samplers (via reverse lookup of multiple spirv name levels)
      //this one particular regex performs like a nitemare on msvc DEBUG heap
      //using regular search instead
      auto findLeftHandExpression = [=](const std::string &expression, size_t offset=0)->pair<string, int> {
        auto pos = fragmentShaderAsssembly.find(expression, offset);
        auto foundPos = pos;
        string ret;
        if(pos != string::npos)
        {
          pos--;
          while(fragmentShaderAsssembly[pos] != '\n' && fragmentShaderAsssembly[pos] != ' ')
            ret += fragmentShaderAsssembly[pos--];
          return { string(ret.rbegin(), ret.rend()), (int)foundPos };
        }

        return { ret, -1 };
      };

      auto findImages = [=, &result](string strType, UniformType type) {
        size_t imageSearchPos = 0;
        bool foundImage = false;
        do
        {
          auto match = findLeftHandExpression(" = OpTypeImage %float " + strType, imageSearchPos);
          imageSearchPos = match.second+1;
          foundImage = false;

          if(!match.first.empty())
          {
            match = findLeftHandExpression(" = OpTypeSampledImage " + match.first);
            if(!match.first.empty())
            {
              match = findLeftHandExpression(" = OpTypePointer UniformConstant " + match.first);
              if(!match.first.empty())
              {
                //what a monumental-sized pain in my fucking ass
                bool foundVar = false;
                size_t varSearchPos = 0;
                do
                {
                  auto varMatch = findLeftHandExpression(" = OpVariable " + match.first, varSearchPos);
                  foundVar = false;

                  if(!varMatch.first.empty())
                  {
                    string imageNameExplicit = explicify(varMatch.first);
                    int imageBinding = 0;

                    //look for binding numbers for images and return them in the "offset" field
                    string bindingPattern = (string)"OpDecorate " + imageNameExplicit + " Binding (.*?)\n";
                    auto bm = findPatternMatches(fragmentShaderAsssembly, bindingPattern);
                    if(!bm.empty())
                    {
                      try { imageBinding = stoi(bm[0][0]); } catch(...) { }
                    }

                    auto vec = findPatternMatches(fragmentShaderAsssembly, "OpName " + imageNameExplicit + " \"(.*?)\"");
                    if(!vec.empty())
                    {
                      result.push_back({ imageBinding, 0, vec[0][0], (uint32_t)type, 0 });
                    }

                    varSearchPos = varMatch.second+1;
                    foundVar = true;
                  }
                } while(foundVar);
              }
            }
            foundImage = true;
          }
        } while(foundImage);
      };

      findImages("2D", UT_SAMPLER_2D);
      findImages("Cube", UT_SAMPLER_CUBE);

      // ([^\n]*) = OpTypeImage [%]float 2D
      /*auto vec = findPatternMatches(fragmentShaderAsssembly, "([^ ]*) = OpTypeImage [%]float 2D");
      for(auto imageMatch : vec)
      {
        vec = findPatternMatches(fragmentShaderAsssembly, "[ ]*?([^\n]*) = OpTypeSampledImage "+ explicify(imageMatch[0]));
        if(!vec.empty())
        {
          vec = findPatternMatches(fragmentShaderAsssembly, "([^\n]*)[ ]*? = OpTypePointer UniformConstant "+ explicify(vec[0][0]));
          if(!vec.empty())
          {
            vec = findPatternMatches(fragmentShaderAsssembly, "([^\n]*)[ ]*? = OpVariable " + explicify(vec[0][0]));
            if(!vec.empty())
            {
              vec = findPatternMatches(fragmentShaderAsssembly, "OpName " + explicify(vec[0][0]) + " \"(.*?)\"");
              if(!vec.empty())
              {
                result.push_back({ 0, vec[0][0], UT_SAMPLER_2D });
              }
            }
          }
        }
      }*/

      currentUniformMemberInfos = result;
      return result;
    }
  }
}
