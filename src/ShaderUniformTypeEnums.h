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

//These are only utilized by the high-level introspection VulkanShaderProgram::getUniformBufferMemberInfos() APIs 
//to identify UBO block member types and sampler types..
//On my engine, for compat with GL-code, they must have same values as their OpenGL enum counterparts

namespace vgl
{
  namespace core
  {

#ifdef VGL_VULKAN_CORE_STANDALONE
    enum UniformType : uint32_t
    {
      UT_FLOAT,
      UT_FLOAT_VEC2,
      UT_FLOAT_VEC3,
      UT_FLOAT_VEC4,
      UT_INT,
      UT_INT_VEC2,
      UT_INT_VEC3,
      UT_INT_VEC4,
      UT_BOOL,
      UT_BOOL_VEC2,
      UT_BOOL_VEC3,
      UT_BOOL_VEC4,
      UT_FLOAT_MAT2,
      UT_FLOAT_MAT3,
      UT_FLOAT_MAT4,
      UT_SAMPLER_2D,
      UT_SAMPLER_CUBE
    };
#else
    enum UniformType : uint32_t
    {
      UT_FLOAT = GL_FLOAT,
      UT_FLOAT_VEC2 = GL_FLOAT_VEC2,
      UT_FLOAT_VEC3 = GL_FLOAT_VEC3,
      UT_FLOAT_VEC4 = GL_FLOAT_VEC4,
      UT_INT = GL_INT,
      UT_INT_VEC2 = GL_INT_VEC2,
      UT_INT_VEC3 = GL_INT_VEC3,
      UT_INT_VEC4 = GL_INT_VEC4,
      UT_BOOL = GL_BOOL,
      UT_BOOL_VEC2 = GL_BOOL_VEC2,
      UT_BOOL_VEC3 = GL_BOOL_VEC3,
      UT_BOOL_VEC4 = GL_BOOL_VEC4,
      UT_FLOAT_MAT2 = GL_FLOAT_MAT2,
      UT_FLOAT_MAT3 = GL_FLOAT_MAT3,
      UT_FLOAT_MAT4 = GL_FLOAT_MAT4,
      UT_SAMPLER_2D = GL_SAMPLER_2D,
      UT_SAMPLER_CUBE = GL_SAMPLER_CUBE,
    };
#endif

  }
}