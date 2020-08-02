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
#include <assert.h>
#include <cmath>
#include "VecTypes.h"

using namespace std;

const float4 float4::zero = { 0, 0, 0, 0 };
const float4 float4::none = { INFINITY, INFINITY, INFINITY, INFINITY };

float4 float4::operator+(const float4 &rhs) const
{
  float4 ret = { x+rhs.x, y+rhs.y, z+rhs.z, w+rhs.w };
  return ret;
}

float4 float4::operator-(const float4 &rhs) const
{
  float4 ret = { x-rhs.x, y-rhs.y, z-rhs.z, w-rhs.w };
  return ret;
}

float4 float4::operator*(const float4 &rhs) const
{
  float4 ret = { x*rhs.x, y*rhs.y, z*rhs.z, w*rhs.w };
  return ret;
}

float4 float4::operator*(const float rhs) const
{
  float4 ret = { x*rhs, y*rhs, z*rhs, w*rhs };
  return ret;
}

float4 &float4::operator +=(const float4 &rhs)
{
  x += rhs.x;
  y += rhs.y;
  z += rhs.z;
  w += rhs.w;
  
  return *this;
}

float4 &float4::operator -=(const float4 &rhs)
{
  x -= rhs.x;
  y -= rhs.y;
  z -= rhs.z;
  w -= rhs.w;
  
  return *this;
}

float4 &float4::operator *=(const float4 &rhs)
{
  x *= rhs.x;
  y *= rhs.y;
  z *= rhs.z;
  w *= rhs.w;
  
  return *this;
}

float4 &float4::operator *=(const float rhs)
{
  x *= rhs;
  y *= rhs;
  z *= rhs;
  w *= rhs;
  
  return *this;
}

bool float4::operator ==(const float4 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z && w==rhs.w);
}

bool float4::operator !=(const float4 &rhs) const
{
  return !(*this == rhs);
}

static float dummy = 0;
float &float4::operator[](int index)
{
  assert(index < 4 && index >= 0);
  switch(index)
  {
    case 0:
      return x;
    break;
    case 1:
      return y;
    break;
    case 2:
      return z;
    break;
    case 3:
      return w;
    break;
    default:
    break;
  }
  
  return dummy;
}

float float4::dot(const float4 &rhs) const
{
  return x*rhs.x + y*rhs.y + z*rhs.z + w*rhs.w;
}

float float4::length() const
{
  return sqrtf(dot(*this));
}

float float4::lengthSquared() const
{
  return dot(*this);
}

float4 float4::normalized() const
{
  float len = length();
  return float4(x/len, y/len, z/len, w/len);
}

void float4::transform(const float *matrix)
{
  float vin[4] = { x, y, z, w }, v_out[4] = { 0, 0, 0, 0 };
  
  //for each column of m
  for(int i = 0; i < 4; i++)
  {
    //add up the linear combination
    v_out[0] += vin[i]*matrix[i*4+0];
    v_out[1] += vin[i]*matrix[i*4+1];
    v_out[2] += vin[i]*matrix[i*4+2];
    v_out[3] += vin[i]*matrix[i*4+3];
  }
  
  x = v_out[0];
  y = v_out[1];
  z = v_out[2];
  w = v_out[3];
}

std::ostream &operator <<(std::ostream &ostr, const float4 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
  
  return ostr;
}

////////////////////////////////////////////////////////////////////////////////////////////////

const float3 float3::zero = { 0, 0, 0 };
const float3 float3::none = { INFINITY, INFINITY, INFINITY };

float3 float3::operator-() const
{
  return (*this) * -1.0f;
}

float3 float3::operator+(const float3 &rhs) const
{
  float3 ret = { x+rhs.x, y+rhs.y, z+rhs.z };
  return ret;
}

float3 float3::operator-(const float3 &rhs) const
{
  float3 ret = { x-rhs.x, y-rhs.y, z-rhs.z };
  return ret;
}

float3 float3::operator*(const float3 &rhs) const
{
  float3 ret = { x*rhs.x, y*rhs.y, z*rhs.z };
  return ret;
}

float3 float3::operator*(const float rhs) const
{
  float3 ret = { x*rhs, y*rhs, z*rhs };
  return ret;
}

float3 float3::operator/(const float3 &rhs) const
{
  float3 ret = { x/rhs.x, y/rhs.y, z/rhs.z };
  return ret;
}

float3 float3::operator/(const float rhs) const
{
  float3 ret = { x/rhs, y/rhs, z/rhs };
  return ret;
}

float3 &float3::operator +=(const float3 &rhs)
{
  x += rhs.x;
  y += rhs.y;
  z += rhs.z;
  
  return *this;
}

float3 &float3::operator -=(const float3 &rhs)
{
  x -= rhs.x;
  y -= rhs.y;
  z -= rhs.z;
  
  return *this;
}

float3 &float3::operator *=(const float3 &rhs)
{
  x *= rhs.x;
  y *= rhs.y;
  z *= rhs.z;
  
  return *this;
}

float3 &float3::operator *=(const float rhs)
{
  x *= rhs;
  y *= rhs;
  z *= rhs;
  
  return *this;
}

float3 &float3::operator /=(const float3 &rhs)
{
  x /= rhs.x;
  y /= rhs.y;
  z /= rhs.z;

  return *this;
}

float3 &float3::operator /=(const float rhs)
{
  x /= rhs;
  y /= rhs;
  z /= rhs;

  return *this;
}

bool float3::operator ==(const float3 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z);
}

bool float3::operator !=(const float3 &rhs) const
{
  return !(*this == rhs);
}

float &float3::operator[](int index)
{
  assert(index < 3 && index >= 0);
  switch(index)
  {
    case 0:
      return x;
    break;
    case 1:
      return y;
    break;
    case 2:
      return z;
    break;
    default:
    break;
  }
  
  return dummy;
}

const float &float3::operator[](int index) const
{
  assert(index < 3 && index >= 0);
  switch(index)
  {
    case 0:
      return x;
    break;
    case 1:
      return y;
    break;
    case 2:
      return z;
    break;
    default:
    break;
  }

  return dummy;
}

float float3::dot(const float3 &rhs) const
{
  return x*rhs.x + y*rhs.y + z*rhs.z;
}

float3 float3::cross(const float3 &rhs) const
{
  float3 r = float3::zero;
  r.x = (y * rhs.z) - (z * rhs.y);
  r.y = (z * rhs.x) - (x * rhs.z);
  r.z = (x * rhs.y) - (y * rhs.x);
  
  return r;
}

float float3::length() const
{
  return sqrtf(dot(*this));
}

float float3::lengthSquared() const
{
  return dot(*this);
}

float3 float3::normalized() const
{
  float len = length();
  return float3(x/len, y/len, z/len);
}

void float3::transform(const float *matrix)
{
  float vin[4] = { x, y, z, 1 }, v_out[4] = { 0, 0, 0, 0 };
  
  //for each column of m
  for(int i = 0; i < 4; i++)
  {
    //add up the linear combination
    v_out[0] += vin[i]*matrix[i*4+0];
    v_out[1] += vin[i]*matrix[i*4+1];
    v_out[2] += vin[i]*matrix[i*4+2];
    v_out[3] += vin[i]*matrix[i*4+3];
  }
  
  x = v_out[0];
  y = v_out[1];
  z = v_out[2];
}

float3 float3::normalVector(float3 vec[3])
{
  float3 a = vec[1]-vec[0];
  float3 b = vec[1]-vec[2];
  
  return b.cross(a).normalized();
}

float3 float3::tangentVector(float3 vec[3], float2 tc[3])
{
  //pretty dumb that I'm recomputing this currently
  //If I ever do, remember that I AM SWAPPING THESE b,a
  float3 b = vec[1]-vec[0];
  float3 a = vec[1]-vec[2];
  
  float2 st[2];
  st[0] = float2(tc[1].x-tc[2].x, tc[1].y-tc[2].y);
  st[1] = float2(tc[1].x-tc[0].x, tc[1].y-tc[0].y);
  
  float coef = 1.0f / (st[0].x * st[1].y - st[1].x * st[0].y);
  float3 tangent;
  
  tangent.x = coef * ((a.x * st[1].y)  + (b.x * -st[0].y));
  tangent.y = coef * ((a.y * st[1].y)  + (b.y * -st[0].y));
  tangent.z = coef * ((a.z * st[1].y)  + (b.z * -st[0].y));
  
  float3 ret = tangent.normalized();
  
  //todo: someday, get handedness and give it to shader
  // Calculate handedness (http://www.gamedev.net/topic/481517-glsl-bump-mapping--is-tangent-needed/)
  //tangent[a].w = (Dot(Cross(n, t), tan2[a]) < 0.0F) ? -1.0F : 1.0F;
  
  return ret;
}

std::ostream &operator <<(std::ostream &ostr, const float3 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  
  return ostr;
}

////////////////////////////////////////////////////////////////////////////////////////////////

const float2 float2::zero = { 0, 0 };
const float2 float2::none = { INFINITY, INFINITY };

float2 float2::operator +(const float2 &rhs) const
{
  float2 ret = { x+rhs.x, y+rhs.y };
  return ret;
}

float2 float2::operator -(const float2 &rhs) const
{
  float2 ret = { x-rhs.x, y-rhs.y };
  return ret;
}

float2 float2::operator *(const float2 &rhs) const
{
  float2 ret = { x*rhs.x, y*rhs.y };
  return ret;
}

float2 float2::operator *(const float rhs) const
{
  float2 ret = { x*rhs, y*rhs };
  return ret;
}

float2 &float2::operator +=(const float2 &rhs)
{
  x += rhs.x;
  y += rhs.y;
  
  return *this;
}

float2 &float2::operator -=(const float2 &rhs)
{
  x -= rhs.x;
  y -= rhs.y;
  
  return *this;
}

float2 &float2::operator *=(const float2 &rhs)
{
  x *= rhs.x;
  y *= rhs.y;
  
  return *this;
}

float2 &float2::operator *=(const float rhs)
{
  x *= rhs;
  y *= rhs;
  
  return *this;
}

bool float2::operator ==(const float2 &rhs)
{
  return (x==rhs.x && y==rhs.y);
}

bool float2::operator !=(const float2 &rhs)
{
  return !(*this == rhs);
}

float &float2::operator[](int index)
{
  assert(index < 2 && index >= 0);
  switch(index)
  {
    case 0:
      return x;
    break;
    case 1:
      return y;
    break;
    default:
    break;
  }
  
  return dummy;
}

float float2::dot(const float2 &rhs) const
{
  return x*rhs.x + y*rhs.y;
}

float float2::length() const
{
  return sqrtf(dot(*this));
}

float float2::lengthSquared() const
{
  return dot(*this);
}

float2 float2::normalized() const
{
  float len = length();
  return float2(x/len, y/len);
}

bool float2::isLeft(float2 a, float2 b, float2 p)
{
  return ((b.x - a.x)*(p.y - a.y) - (b.y - a.y)*(p.x - a.x)) > 0;
}

std::ostream &operator <<(std::ostream &ostr, const float2 &v)
{
  ostr << "(" << v.x << ", " << v.y << ")";
  
  return ostr;
}

////////////////////////////////////////////////////////////////////////////////////////////////

std::ostream &operator <<(std::ostream &ostr, const int2 &v)
{
  ostr << "(" << v.x << ", " << v.y << ")";
  
  return ostr;
}

std::ostream &operator <<(std::ostream &ostr, const uint2 &v)
{
  ostr << "(" << v.x << ", " << v.y << ")";
  
  return ostr;
}

std::ostream &operator <<(std::ostream &ostr, const int3 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ")";

  return ostr;
}

std::ostream &operator <<(std::ostream &ostr, const uint3 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ")";

  return ostr;
}

std::ostream &operator <<(std::ostream &ostr, const int4 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
  
  return ostr;
}

std::ostream &operator <<(std::ostream &ostr, const uint4 &v)
{
  ostr << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
  
  return ostr;
}

bool int4::operator ==(const int4 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z && w==rhs.w);
}

bool int4::operator !=(const int4 &rhs) const
{
  return !(*this == rhs);
}

bool uint4::operator ==(const uint4 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z && w==rhs.w);
}

bool uint4::operator !=(const uint4 &rhs) const
{
  return !(*this == rhs);
}

bool int3::operator ==(const int3 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z);
}

bool int3::operator !=(const int3 &rhs) const
{
  return !(*this == rhs);
}

bool uint3::operator ==(const uint3 &rhs) const
{
  return (x==rhs.x && y==rhs.y && z==rhs.z);
}

bool uint3::operator !=(const uint3 &rhs) const
{
  return !(*this == rhs);
}

bool int2::operator ==(const int2 &rhs) const
{
  return (x==rhs.x && y==rhs.y);
}

bool int2::operator !=(const int2 &rhs) const
{
  return !(*this == rhs);
}

bool uint2::operator ==(const uint2 &rhs) const
{
  return (x==rhs.x && y==rhs.y);
}

bool uint2::operator !=(const uint2 &rhs) const
{
  return !(*this == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////
