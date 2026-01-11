cmake_minimum_required(VERSION 3.24)

project(HairTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HAIRTOOL_ENABLE_GL_DEBUG "Enable OpenGL debug output" ON)
option(HAIRTOOL_ENABLE_CUDA "Enable CUDA hair solver backend" ON)

if (HAIRTOOL_ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    # RTX 3090 = sm_86
    if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      set(CMAKE_CUDA_ARCHITECTURES 86)
    endif()
  else()
    message(WARNING "CUDA toolset not found; disabling HAIROOL_ENABLE_CUDA and building CPU-only.")
    set(HAIRTOOL_ENABLE_CUDA OFF CACHE BOOL "Enable CUDA hair solver backend" FORCE)
  endif()
endif()

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_executable(HairTool
  src/main.cpp
  src/App.cpp
  src/App.h
  src/UserSettings.cpp
  src/UserSettings.h
  src/Camera.cpp
  src/Camera.h
  src/MayaCameraController.cpp
  src/MayaCameraController.h
  src/GL.cpp
  src/GL.h
  src/Renderer.cpp
  src/Renderer.h
  src/Mesh.cpp
  src/Mesh.h
  src/Bvh.cpp
  src/Bvh.h
  src/Raycast.cpp
  src/Raycast.h
  src/HairGuides.cpp
  src/HairGuides.h
  src/Physics.cpp
  src/Physics.h
  src/Scene.cpp
  src/Scene.h
  src/Serialization.cpp
  src/Serialization.h
  src/ExportPly.cpp
  src/ExportPly.h
  src/FileDialog.cpp
  src/FileDialog.h
  src/GpuSolver.cpp
  src/GpuSolver.h
  src/MeshDistanceField.cpp
  src/MeshDistanceField.h
)

if (HAIRTOOL_ENABLE_CUDA)
  target_sources(HairTool PRIVATE
    src/cuda/CudaHairSolver.h
    src/cuda/CudaHairSolver.cu
  )
  set_source_files_properties(src/cuda/CudaHairSolver.cu PROPERTIES LANGUAGE CUDA)
  set_target_properties(HairTool PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
  target_compile_definitions(HairTool PRIVATE HAIRTOOL_ENABLE_CUDA=1)
  target_link_libraries(HairTool PRIVATE CUDA::cudart)
endif()

target_include_directories(HairTool PRIVATE src)

target_link_libraries(HairTool PRIVATE
  glfw
  glad::glad
  imgui::imgui
  glm::glm
  assimp::assimp
  JsonCpp::JsonCpp
)

target_compile_definitions(HairTool PRIVATE
  IMGUI_IMPL_OPENGL_LOADER_GLAD
)

if(MSVC)
  target_compile_options(HairTool PRIVATE /W4 /permissive-)
else()
  target_compile_options(HairTool PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HAIRTOOL_ENABLE_GL_DEBUG)
  target_compile_definitions(HairTool PRIVATE HAIRTOOL_ENABLE_GL_DEBUG=1)
endif()
