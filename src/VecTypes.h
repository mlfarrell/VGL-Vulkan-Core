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

#ifndef PREALIGNED16
#if _MSC_VER && !__INTEL_COMPILER
#ifdef _WIN64
#define VGLINLINE __inline
#define PREALIGNED16 __declspec(align(16))
#define POSTALIGNED16
#else
#define PREALIGNED16
#define POSTALIGNED16
#endif
#else
#define VGLINLINE __inline__
#define PREALIGNED16
#define POSTALIGNED16 __attribute__((aligned(16)))
#endif
#endif

#include <iostream>

struct float2
{
  float x, y;
  
  static const float2 zero, none;

  ///No implicit initialization is assumed even when using default constructor
  float2()
  {
    //intentionally not initializing these
  }
  
  float2(float v[2])
  {
    x = v[0];
    y = v[1];
  }
  
  float2(float xv, float yv)
  {
    x = xv;
    y = yv;
  }
  
  float2 operator +(const float2 &rhs) const;
  float2 operator -(const float2 &rhs) const;
  float2 operator *(const float2 &rhs) const;
  float2 operator *(const float rhs) const;
  
  float2 &operator +=(const float2 &rhs);
  float2 &operator -=(const float2 &rhs);
  float2 &operator *=(const float2 &rhs);
  float2 &operator *=(const float rhs);
  
  bool operator ==(const float2 &rhs);
  bool operator !=(const float2 &rhs);
  
  float &operator[](int index);
  float dot(const float2 &rhs) const;
  
  float length() const;
  float lengthSquared() const;
  
  float2 normalized() const;
  
  static bool isLeft(float2 a, float2 b, float2 p);

};
std::ostream &operator <<(std::ostream &ostr, const float2 &v);

struct float3
{
  float x, y, z;
  
  ///No implicit initialization is assumed even when using default constructor
  float3()
  {
    //intentionally not initializing these
  }
  
  float3(float v[3])
  {
    x = v[0];
    y = v[1];
    z = v[2];
  }
  
  float3(float xv, float yv, float zv)
  {
    x = xv;
    y = yv;
    z = zv;
  }
  
  static const float3 zero, none;

  float3 operator -() const;

  float3 operator +(const float3 &rhs) const;
  float3 operator -(const float3 &rhs) const;
  float3 operator *(const float3 &rhs) const;
  float3 operator *(const float rhs) const;
  float3 operator /(const float3 &rhs) const;
  float3 operator /(const float rhs) const;

  float3 &operator +=(const float3 &rhs);
  float3 &operator -=(const float3 &rhs);
  float3 &operator *=(const float3 &rhs);
  float3 &operator *=(const float rhs);
  float3 &operator /=(const float3 &rhs);
  float3 &operator /=(const float rhs);

  bool operator ==(const float3 &rhs) const;
  bool operator !=(const float3 &rhs) const;
  
  float &operator[](int index);
  const float &operator[](int index) const;

  inline float2 xz() const { float2 ret = {x, z}; return ret; }
  inline float2 xy() const { float2 ret = {x, y}; return ret; }
  inline float2 yz() const { float2 ret = {y, z}; return ret; }
  
  float dot(const float3 &rhs) const;
  float3 cross(const float3 &rhs) const;
  
  float length() const;
  float lengthSquared() const;
  
  float3 normalized() const;
  
  void transform(const float *matrix4x4);
  
  static float3 normalVector(float3 vec[3]);
  static float3 tangentVector(float3 vec[3], float2 tc[3]);
};
std::ostream &operator <<(std::ostream &ostr, const float3 &v);

PREALIGNED16 struct float4
{
  union
  {
    struct
    {
      float x, y, z, w;
    };
    struct
    {
      float r, g, b, a;
    };
  };
  
  ///No implicit initialization is assumed even when using default constructor
  float4()
  {
    //intentionally not initializing these
  }
  
  float4(float3 v3, float wv)
  {
    x = v3.x;
    y = v3.y;
    z = v3.z;
    w = wv;
  }
  
  float4(float v[4])
  {
    x = v[0];
    y = v[1];
    z = v[2];
    w = v[3];
  }
  
  float4(float xv, float yv, float zv, float wv)
  {
    x = xv;
    y = yv;
    z = zv;
    w = wv;
  }
  
  static const float4 zero, none;
  
  float4 operator +(const float4 &rhs) const;
  float4 operator -(const float4 &rhs) const;
  float4 operator *(const float4 &rhs) const;
  float4 operator *(const float rhs) const;
  
  float4 &operator +=(const float4 &rhs);
  float4 &operator -=(const float4 &rhs);
  float4 &operator *=(const float4 &rhs);
  float4 &operator *=(const float rhs);
  
  bool operator ==(const float4 &rhs) const;
  bool operator !=(const float4 &rhs) const;
  
  float &operator[](int index);

  inline float2 xz() const { float2 ret = {x, z}; return ret; }
  inline float2 xy() const { float2 ret = {x, y}; return ret; }
  inline float2 yz() const { float2 ret = {y, z}; return ret; }
  inline float3 xyz() const { float3 ret = {x, y, z}; return ret; }

  float dot(const float4 &rhs) const;
  
  float length() const;
  float lengthSquared() const;
  
  float4 normalized() const;
  
  void transform(const float *matrix4x4);
} POSTALIGNED16;
std::ostream &operator <<(std::ostream &ostr, const float4 &v);

struct float8
{
  float x, y, z, w, a, b, c, d;
};

struct float16
{
  float a1, a2, a3, a4;
  float b1, b2, b3, b4;
  float c1, c2, c3, c4;
  float d1, d2, d3, d4;
};

struct ushort3
{
  unsigned short x, y, z;
  
  ushort3()
  {
    //intentionally not initializing these
  }
  
  ushort3(unsigned short xv, unsigned short yv, unsigned short zv)
  {
    x = xv;
    y = yv;
    z = zv;
  }
};

struct int2
{
  int x, y;
  
  int2()
  {
    //intentionally not initializing these
  }
  
  int2(int xv, int yv)
  {
    x = xv;
    y = yv;
  }
  
  bool operator ==(const int2 &rhs) const;
  bool operator !=(const int2 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const int2 &v);

struct int3
{
  int x, y, z;
  
  int3()
  {
    //intentionally not initializing these
  }
  
  int3(int xv, int yv, int zv)
  {
    x = xv;
    y = yv;
    z = zv;
  }

  bool operator ==(const int3 &rhs) const;
  bool operator !=(const int3 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const int3 &v);

struct int4
{
  int x, y, z, w;
  
  int4()
  {
    //intentionally not initializing these
  }
  
  int4(int xv, int yv, int zv, int wv)
  {
    x = xv;
    y = yv;
    z = zv;
    w = wv;
  }
  
  bool operator ==(const int4 &rhs) const;
  bool operator !=(const int4 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const int4 &v);

struct uint2
{
  unsigned int x, y;
  
  uint2()
  {
    //intentionally not initializing these
  }
  
  uint2(unsigned int xv, unsigned int yv)
  {
    x = xv;
    y = yv;
  }
  
  bool operator ==(const uint2 &rhs) const;
  bool operator !=(const uint2 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const uint2 &v);

struct uint3
{
  unsigned int x, y, z;
  
  uint3()
  {
    //intentionally not initializing these
  }
  
  uint3(unsigned int xv, unsigned int yv, unsigned int zv)
  {
    x = xv;
    y = yv;
    z = zv;
  }

  bool operator ==(const uint3 &rhs) const;
  bool operator !=(const uint3 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const uint3 &v);

struct uint4
{
  unsigned int x, y, z, w;
  
  uint4()
  {
    //intentionally not initializing these
  }
  
  uint4(unsigned int xv, unsigned int yv, unsigned int zv, unsigned int wv)
  {
    x = xv;
    y = yv;
    z = zv;
    w = wv;
  }
  
  bool operator ==(const uint4 &rhs) const;
  bool operator !=(const uint4 &rhs) const;
};
std::ostream &operator <<(std::ostream &ostr, const uint4 &v);

static inline uint3 uint3_reverse(unsigned int x, unsigned int y, unsigned int z)
{
  uint3 ret = { z, y, x };
  return ret;
}

static inline void reverse_uint3_order(uint3 *tri)
{
  unsigned int tmp = tri->x;
  tri->x = tri->z;
  tri->z = tmp;
}

//Generates the quad in the proper order for GL_TRIANGLE_STRIP quad emulation
static inline uint4 uint4_tri_strip(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
  uint4 ret = { x, y, w, z };
  return ret;
}

static inline uint4 uint4_reverse(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
  uint4 ret = { w, z, y, x };
  return ret;
}

static inline void reverse_uint4_order(uint4 *quad)
{
  uint4 reversed = uint4_reverse(quad->x, quad->y, quad->z, quad->w);
  *quad = reversed;
}

