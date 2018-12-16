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
#define PREALIGNED16  //__declspec(align(x))
#define POSTALIGNED16 
#endif
#else
#define VGLINLINE __inline__
#define PREALIGNED16
#define POSTALIGNED16 __attribute__((aligned(16)))
#endif
#endif

#ifdef ANDROID
//build by default with support only for short indices
#define UINT3_IS_SHORT 1
#else
#define UINT3_IS_SHORT 0
#endif

#include <iostream>

typedef struct float2
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

} float2;
std::ostream &operator <<(std::ostream &ostr, const float2 &v);

typedef struct float3
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
} float3;

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

  //not exhaustive - just commonly used ones
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

#ifndef ANDROID
typedef struct float8
{
  union
  {
    struct
    {
      float x, y, z, w, a, b, c, d;
    };
    struct
    {
      float4 hi, lo;
    };
  };
  
} float8;
#else
typedef struct float8
{
  float x, y, z, w, a, b, c, d;
} float8;
#endif

typedef struct float16
{
	float a1, a2, a3, a4;
	float b1, b2, b3, b4;
	float c1, c2, c3, c4;
	float d1, d2, d3, d4;
} float16;

typedef struct ushort3
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
} ushort3;

typedef struct int2
{
  int x, y;
} int2;

typedef struct int3
{
  int x, y, z;

  bool operator ==(const int3 &rhs) const;
  bool operator !=(const int3 &rhs) const;
} int3;

typedef struct int4
{
  int x, y, z, w;
} int4;

typedef struct uint2
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
  
  bool operator ==(const uint2 &rhs);
  bool operator !=(const uint2 &rhs);
  
} uint2;

typedef struct uint3
{
#if !UINT3_IS_SHORT
  unsigned int x, y, z;
#else
  unsigned short x, y, z;
#endif
  
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

} uint3;

typedef struct uint4
{
#if !UINT3_IS_SHORT
  unsigned int x, y, z, w;
#else
  unsigned short x, y, z, w;
#endif
  
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
} uint4;

static inline float4 make_float4(float x, float y, float z, float w)
{
  float4 ret = { x, y, z, w };
  return ret;
}

static inline float4 make_float4v(float3 v, float w)
{
  float4 ret = { v.x, v.y, v.z, w };
  return ret;
}


static inline float3 make_float3(float x, float y, float z)
{
  float3 ret = { x, y, z };
  return ret;
}

static inline float2 make_float2(float x, float y)
{
  float2 ret = { x, y };
  return ret;
}

static inline ushort3 make_ushort3(unsigned short x, unsigned short y, unsigned short z)
{
  ushort3 ret = { x, y, z };
  return ret;
}

static inline ushort3 make_ushort3_reverse(unsigned short x, unsigned short y, unsigned short z)
{
  ushort3 ret = { z, y, x };
  return ret;
}

static inline void reverse_ushort3_order(ushort3 *tri)
{
  unsigned short tmp = tri->x;
  tri->x = tri->z;
  tri->z = tmp;
}

static inline uint2 make_uint2(unsigned int x, unsigned int y)
{
  uint2 ret = { x, y };
  return ret;
}

static inline uint3 make_uint3(unsigned int x, unsigned int y, unsigned int z)
{
  uint3 ret = { x, y, z };
  return ret;
}

static inline uint3 make_uint3_reverse(unsigned int x, unsigned int y, unsigned int z)
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

static inline uint4 make_uint4(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
  uint4 ret = { x, y, z, w };
  return ret;
}

//Generates the quad in the proper order for GL_TRIANGLE_STRIP quad emulation
static inline uint4 make_uint4_tri_strip(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
  uint4 ret = { x, y, w, z };
  return ret;
}

static inline uint4 make_uint4_reverse(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
  uint4 ret = { w, z, y, x };
  return ret;
}

static inline void reverse_uint4_order(uint4 *quad)
{
  uint4 reversed = make_uint4_reverse(quad->x, quad->y, quad->z, quad->w);
  *quad = reversed;
}

static inline int2 make_int2(int x, int y)
{
  int2 ret = { x, y };
  return ret;
}

static inline int3 make_int3(int x, int y, int z)
{
  int3 ret = { x, y, z };
  return ret;
}

static inline int4 make_int4(int x, int y, int z, int w)
{
  int4 ret = { x, y, z, w };
  return ret;
}

#define FLOAT4_EQUAL(vec1, vec2)   (vec1.x == vec2.x && vec1.y == vec2.y && vec1.z == vec2.z && vec1.w == vec2.w)
#define FLOAT3_EQUAL(vec1, vec2)   (vec1.x == vec2.x && vec1.y == vec2.y && vec1.z == vec2.z)
