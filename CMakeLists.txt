#this file is NOT YET ready for public use yet
#its eventual intent is to provide a .lib build of the core quickly from the command line

cmake_minimum_required(VERSION 3.7)

project(VGLVulkanCore)

set(VULKANSDK_ARCH "x86_64")

#TODO make this less dumb
if(DEFINED BUILD_SURFACE_X11)
  set(surface_INCLUDE_DIRS "")
  set(surface_LIBRARIES "-lX11")
elseif(DEFINED BUILD_SURFACE_macOS)
  set(surface_INCLUDE_DIRS "")
  set(surface_LIBRARIES "-framework Cocoa")
else()
  set(surface_INCLUDE_DIRS "")
  set(surface_LIBRARIES "")
endif()

include_directories(
  ${surface_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}  
)

if(DEFINED VMA_PATH)
  message(STATUS "Using VMA for memory management...")

  include_directories(
    ${VMA_PATH}
  )  
  add_definitions(
    -DVGL_VULKAN_CORE_USE_VMA
  )  
endif()

if(NOT DEFINED VULKANSDK)
  message(FATAL_ERROR "Must be called with -DVULKANSDK=/path/to/vulkansdk")
endif()

include_directories(
	"${VULKANSDK}/${VULKANSDK_ARCH}/include/vulkan"
	"${CMAKE_SOURCE_DIR}/src/"
	"${CMAKE_SOURCE_DIR}/src/os"
)

if(APPLE)
  include_directories(
    "${CMAKE_SOURCE_DIR}/src/os/mac" 
    "${VULKANSDK}/MoltenVK/include/vulkan"
  	"${CMAKE_SOURCE_DIR}/example/mac/Example/src" # TODO move this header  
  )

  set(vglsurface "${CMAKE_SOURCE_DIR}/src/os/mac/VulkanSurface.mm")  
  add_definitions(-DMACOSX)
elseif(UNIX)
  include_directories(
    "${CMAKE_SOURCE_DIR}/src/os/linux" 
    "${CMAKE_SOURCE_DIR}/example/linux"
  )
  include_directories("${CMAKE_SOURCE_DIR}/src/os/linux")

  set(vglsurface "${CMAKE_SOURCE_DIR}/src/os/linux/VulkanSurface.cpp")
elseif(MSVC)
  #todo
endif()


link_directories(
  "${VULKANSDK}/${VULKANSDK_ARCH}/lib"
)

file(GLOB vglcore "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB vglos "${CMAKE_SOURCE_DIR}/src/os/*.cpp")

add_definitions(
  -DVGL_VULKAN_CORE_STANDALONE
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG)
endif()

add_library(VGLVulkanCore STATIC
  ${vglcore} ${vglos} ${vglsurface}
)

set_property(TARGET VGLVulkanCore PROPERTY CXX_STANDARD 17)

#target_link_libraries(VGLVulkanCore 
#  ${SDL2_LIBRARIES}
#  ${surface_LIBRARIES}
#  vulkan
#)

