# This file is for linux.
# For the windows/mac builds use the already existing projects for those..
# I'm not a CMake expert, don't judge me :p

cmake_minimum_required(VERSION 3.7)

project(VGLVulkanExample)

set(VULKANSDK_ARCH "x86_64")

find_package(SDL2 REQUIRED)

set(x11_INCLUDE_DIRS "")
set(x11_LIBRARIES "-lX11")

include_directories(
  ${x11_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}  
)

if(NOT DEFINED VULKANSDK)
  message(FATAL_ERROR "Must be called with -DVULKANSDK=/path/to/vulkansdk")
endif()

include_directories(
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/.."
	"${CMAKE_SOURCE_DIR}/../include"
	"${VULKANSDK}/${VULKANSDK_ARCH}/include/vulkan"
	"${CMAKE_SOURCE_DIR}/../../src/"
	"${CMAKE_SOURCE_DIR}/../../src/os"
	"${CMAKE_SOURCE_DIR}/../../src/os/linux"
)

link_directories(
  "${VULKANSDK}/${VULKANSDK_ARCH}/lib"
)

file(GLOB vglcore "${CMAKE_SOURCE_DIR}/../../src/*.cpp")
file(GLOB vglos "${CMAKE_SOURCE_DIR}/../../src/os/*.cpp")

add_definitions(
  -DVGL_VULKAN_CORE_STANDALONE
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG)
endif()

add_executable(VGLVulkanExample 
  ${vglcore} ${vglos} ${vgloslinux}
  "${CMAKE_SOURCE_DIR}/../../src/os/linux/VulkanSurface.cpp"   # Xcb version is also available
	"${CMAKE_SOURCE_DIR}/../ExampleRenderer.cpp"	
	"${CMAKE_SOURCE_DIR}/../Example.cpp"  
)

target_link_libraries(VGLVulkanExample 
  ${SDL2_LIBRARIES}
  ${x11_LIBRARIES}
  vulkan
  pthread
)

set(ShaderFiles ${CMAKE_SOURCE_DIR}/../windows/VulkanTesting/glsl)
file(GLOB AssetFiles ${CMAKE_SOURCE_DIR}/../windows/VulkanTesting/banana.jpg)
                      
add_custom_target(copy)
foreach(AssetFile ${AssetFiles})
  add_custom_command(TARGET copy PRE_BUILD
                     COMMAND ${CMAKE_COMMAND} -E
                         copy ${AssetFile} $<TARGET_FILE_DIR:VGLVulkanExample>)
endforeach()

add_custom_command(TARGET copy PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E
                       copy_directory ${ShaderFiles} $<TARGET_FILE_DIR:VGLVulkanExample>/glsl)
                      
message(STATUS "You'll want to run make copy when the build is finished")
# can't get this to work, you'll have to run "make copy" instead
# add_dependencies(VGLVulkanExample copy)
