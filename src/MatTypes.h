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

PREALIGNED16 union _mat4
{
  struct
  {
    float m00, m01, m02, m03;
    float m10, m11, m12, m13;
    float m20, m21, m22, m23;
    float m30, m31, m32, m33;
  };
  float m[16];
} POSTALIGNED16;
typedef union _mat4 mat4;

union _mat3
{
  struct
  {
    float m00, m01, m02;
    float m10, m11, m12;
    float m20, m21, m22;
  };
  float m[9];
};
typedef union _mat3 mat3;

union _mat2
{
  struct
  {
    float m00, m01;
    float m10, m11;
  };
  float m[4];
};
typedef union _mat2 mat2;

