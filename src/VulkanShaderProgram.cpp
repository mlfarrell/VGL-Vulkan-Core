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

#ifndef VGL_VULKAN_CORE_STANDALONE
#define VGL_VULKAN_USE_SPIRV_CROSS
#endif

#include "shaderc/shaderc.hpp"
#include "VulkanInstance.h"
#include "VulkanShaderProgram.h"
#include "VulkanBufferGroup.h"
#include "VulkanPipelineStateCache.h"
#include "ShaderUniformTypeEnums.h"
#ifndef VGL_VULKAN_CORE_STANDALONE
#include "System.h"
#include "StateMachine.h"
#endif
#ifdef VGL_VULKAN_USE_SPIRV_CROSS
#include "spirv_cross.hpp"
#endif

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
      auto &instance = VulkanInstance::currentInstance();
      auto limits = instance.getPhysicalDeviceProperties().limits;

      device = instance.getDefaultDevice();

      minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
      maxUniformBufferRange = limits.maxUniformBufferRange;

#ifndef VGL_VULKAN_CORE_STANDALONE
      auto csm = vgl::StateMachine::machine().getCoreStateMachine();
      pipelineLayout = csm->getCommonPLLayout1();
#endif
    }

    VulkanShaderProgram::VulkanShaderProgram(VkDevice device)
      : device(device)
    {
      auto &instance = VulkanInstance::currentInstance();
      auto limits = instance.getPhysicalDeviceProperties().limits;

      if(!device)
        device = instance.getDefaultDevice();

      minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
      maxUniformBufferRange = limits.maxUniformBufferRange;

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
#ifndef VGL_VULKAN_USE_SPIRV_CROSS
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
#else
          //intentionally not optimizing this step (this spv only used for introspection)
          shaderc::SpvCompilationResult reflectionModule = compiler.CompileGlslToSpv(source, kind, source_name.c_str(), shaderc::CompileOptions());

          if(kind == shaderc_glsl_vertex_shader)
            vertexShaderBin = { reflectionModule.cbegin(), reflectionModule.cend() };
          else if(kind == shaderc_glsl_fragment_shader)
            fragmentShaderBin = { reflectionModule.cbegin(), reflectionModule.cend() };
          else if(kind == shaderc_glsl_geometry_shader)
            geometryShaderBin = { reflectionModule.cbegin(), reflectionModule.cend() };
#endif
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
      if(dynamicState.size > maxUniformBufferRange)
        throw vgl_runtime_error("Exceeded maxUniformBufferRange");
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

        matrix3[0] = { matrix.m[0], matrix.m[1], matrix.m[2], 0 };
        matrix3[1] = { matrix.m[3], matrix.m[4], matrix.m[5], 0 };
        matrix3[2] = { matrix.m[6], matrix.m[7], matrix.m[8], 0 };

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

    //if we have spirv-cross, we can use it to implement our shader introspection support
#ifdef VGL_VULKAN_USE_SPIRV_CROSS
    
    //https://github.com/KhronosGroup/SPIRV-Cross/wiki/Reflection-API-user-guide
    static VulkanShaderProgram::UniformBufferMemberInfo *updateVarInfoFromType(const spirv_cross::Compiler &comp, const spirv_cross::SPIRType &parentType, uint32_t typeId, int memberIndex, vector<VulkanShaderProgram::UniformBufferMemberInfo> &allInfos)
    {
      using namespace spirv_cross;
      
      VulkanShaderProgram::UniformBufferMemberInfo info;
      const auto &type = comp.get_type(typeId);
      bool good = true;
      
      string infoName = comp.get_member_name(parentType.self, memberIndex);
      uint32_t infoOffset = comp.type_struct_member_offset(parentType, memberIndex);
      
      switch(type.basetype)
      {
        case SPIRType::Float:
          if(type.columns == 1)
          {
            if(type.vecsize == 1)
            {
              info.size = 4;
              info.type = UT_FLOAT;
            }
            else if(type.vecsize == 2)
            {
              info.size = 8;
              info.type = UT_FLOAT_VEC2;
            }
            else if(type.vecsize == 3)
            {
              info.size = 16;
              info.type = UT_FLOAT_VEC3;
            }
            else if(type.vecsize == 4)
            {
              info.size = 16;
              info.type = UT_FLOAT_VEC4;
            }
          }
          else
          {
            if(type.columns == 2 && type.vecsize == 2)
            {
              info.type = UT_FLOAT_MAT2;
              info.size = 16;
            }
            else if(type.columns == 3 && type.vecsize == 3)
            {
              info.type = UT_FLOAT_MAT3;
              info.size = 48;
            }
            else if(type.columns == 4 && type.vecsize == 4)
            {
              info.type = UT_FLOAT_MAT4;
              info.size = 64;
            }
          }
        break;
        case SPIRType::Int:
        case SPIRType::UInt:
          if(type.columns == 1)
          {
            if(type.vecsize == 1)
            {
              info.size = 4;
              info.type = UT_INT;
            }
            else if(type.vecsize == 2)
            {
              info.size = 8;
              info.type = UT_INT_VEC2;
            }
            else if(type.vecsize == 3)
            {
              info.size = 16;
              info.type = UT_INT_VEC3;
            }
            else if(type.vecsize == 4)
            {
              info.size = 16;
              info.type = UT_INT_VEC4;
            }
          }
          else
          {
            verr << "Warning:  Unhandled shader var matrix type in (Vulkan) updateVarInfoFromType() (" << (int)type.basetype << ")" << endl;
            good = false;
          }
        break;
        case SPIRType::Boolean:
          if(type.columns == 1)
          {
            if(type.vecsize == 1)
            {
              info.size = 4;
              info.type = UT_BOOL;
            }
            else if(type.vecsize == 2)
            {
              info.size = 8;
              info.type = UT_BOOL_VEC2;
            }
            else if(type.vecsize == 3)
            {
              info.size = 16;
              info.type = UT_BOOL_VEC3;
            }
            else if(type.vecsize == 4)
            {
              info.size = 16;
              info.type = UT_BOOL_VEC4;
            }
          }
          else
          {
            verr << "Warning:  Unhandled shader var matrix type in (Vulkan) updateVarInfoFromType() (" << (int)type.basetype << ")" << endl;
            good = false;
          }
        break;
        case SPIRType::Struct:
        {
          //descend into the nested type
          int index = 0;
          for(auto uboMemberId : type.member_types)
          {
            auto subInfo = updateVarInfoFromType(comp, type, uboMemberId, index, allInfos);
            
            if(subInfo)
            {
              //array AND a struct?  now that's just crazy man..
              if(!type.array.empty())
              {
                size_t array_stride = comp.type_struct_member_array_stride(parentType, memberIndex);
                
                if(type.array.size() > 1)
                {
                  verr << "Error:  Vulkan core cannot handle multidimensional arrays in uniforms!" << endl;
                  return nullptr;
                }
                
                //replicate array entries for this struct field
                auto info0 = *subInfo;

                for(int i = 0; i < type.array[0]; i++)
                {
                  string indexedName = infoName + '[' + to_string(i) + ']';

                  info = info0;
                  info.name = indexedName + '.' + info.name;
                  info.offset += i*array_stride + infoOffset;
                  info.arrayIndex = i;
                  
                  if(i > 0)
                    allInfos.push_back(info);
                  else
                    *subInfo = info;
                }
              }
              else
              {
                subInfo->name = infoName + '.' + subInfo->name;
                subInfo->offset += infoOffset;
              }
            }
            
            index++;
          }
          
          //don't add struct names to the returned infos
          return nullptr;
        }
        break;
        default:
          verr << "Warning:  Unhandled shader var base type in (Vulkan) updateVarInfoFromType() (" << (int)type.basetype << ")" << endl;
          good = false;
        break;
      }
      
      if(good)
      {
        info.name = infoName;
        info.offset = infoOffset;
        info.arrayIndex = 0;
        
        if(!type.array.empty())
        {
          size_t array_stride = comp.type_struct_member_array_stride(parentType, memberIndex);

          if(type.array.size() > 1)
          {
            verr << "Error:  Vulkan core cannot handle multidimensional arrays in uniforms!" << endl;
            return nullptr;
          }

          //replicate array entries
          for(int i = 0; i < type.array[0]; i++)
          {
            info.name = infoName + '[' + to_string(i) + ']';
            info.offset = infoOffset + i*array_stride;
            info.arrayIndex = i;
            
            allInfos.push_back(info);
          }
          
          return &allInfos.back();
        }
        else
        {
          allInfos.push_back(info);
          return &allInfos.back();
        }
      }
      
      return nullptr;
    }
    
    vector<VulkanShaderProgram::UniformBufferMemberInfo> VulkanShaderProgram::getUniformBufferMemberInfos(uint32_t set, uint32_t binding)
    {
      using namespace spirv_cross;
      
      CHECK_INTROSPECTION();
      vector<UniformBufferMemberInfo> result;
      
      if(set == 0 && binding == 0 && !currentUniformMemberInfos.empty())
        return currentUniformMemberInfos;

      Compiler vertexComp(vertexShaderBin), fragmentComp(fragmentShaderBin);
      ShaderResources vtxRes = vertexComp.get_shader_resources();
      Resource *ubo = nullptr;
      
      //look for our uniform buffer
      for(auto &u : vtxRes.uniform_buffers)
      {
        uint32_t uSet = vertexComp.get_decoration(u.id, spv::DecorationDescriptorSet);
        uint32_t uBinding = vertexComp.get_decoration(u.id, spv::DecorationBinding);
        
        if(set == uSet && binding == uBinding)
        {
          ubo = &u;
          break;
        }
      }
      
      if(ubo)
      {
        const SPIRType &type = vertexComp.get_type(ubo->base_type_id);
        
        if(type.basetype == SPIRType::Struct)
        {
          int index = 0;
          
          for(auto uboMemberId : type.member_types)
          {
            updateVarInfoFromType(vertexComp, type, uboMemberId, index, result);
            index++;
          }
        }
      }

      //look for sampled images that we support
      ShaderResources frgRes = fragmentComp.get_shader_resources();

      for(auto &i : frgRes.sampled_images)
      {
        uint32_t iSet = fragmentComp.get_decoration(i.id, spv::DecorationDescriptorSet);
        uint32_t iBinding = fragmentComp.get_decoration(i.id, spv::DecorationBinding);
        const SPIRType &type = fragmentComp.get_type(i.base_type_id);

        if(type.basetype == SPIRType::SampledImage && iSet == 1)
        {
          uint32_t ut = 0;
          
          if(type.image.dim == spv::Dim2D)
            ut = (uint32_t)UT_SAMPLER_2D;
          else if(type.image.dim == spv::DimCube)
            ut = (uint32_t)UT_SAMPLER_CUBE;
          
          if(ut)
            result.push_back({ (int)iBinding, 0, fragmentComp.get_name(i.id), ut, 0 });
        }
      }
      
      currentUniformMemberInfos = result;
      return result;
    }

#else
    //if we don't have spirv-cross, we can fall back on the very terrible spirv assembly regex introspection code
    //using this code below will help you get started without adding yet another dependency, but it is not recommended for speed-critical applications

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
#endif
  }
}
