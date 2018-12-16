#define _USE_MATH_DEFINES 
#include <math.h>
#include <vulkan.h>

#ifdef MACOSX
#include <vulkan_macos.h>
#endif

#ifdef max
#undef max
#endif

//#define MAT_TYPES_VULKAN_DEPTH_RANGE 1

#ifdef VGL_VULKAN_CORE_STANDALONE
#include <iostream>
#define verr cerr
#define vout cout
#define vgl_runtime_error runtime_error

#ifdef DEBUG
#define DebugBuild() 1
#else
#define DebugBuild() 0
#endif
#endif