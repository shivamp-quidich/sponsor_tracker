cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED CMAKE_CXX_COMPILER AND EXISTS "/usr/bin/g++-12")
    set(CMAKE_CXX_COMPILER /usr/bin/g++-12)
endif()
if(NOT DEFINED CMAKE_C_COMPILER AND EXISTS "/usr/bin/gcc-12")
    set(CMAKE_C_COMPILER /usr/bin/gcc-12)
endif()

project(sponsor_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)

set(STIQY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../StiQy_2.0" CACHE PATH "StiQy 2.0 source tree")
set(STIQY_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../StiQy_2.0/build-release" CACHE PATH "StiQy build directory with prebuilt libs")

if(NOT EXISTS "${STIQY_ROOT}/modules/sponsor_grid_tracker")
    message(FATAL_ERROR "STIQY_ROOT does not contain sponsor_grid_tracker: ${STIQY_ROOT}")
endif()
if(NOT EXISTS "${STIQY_BUILD_DIR}/common/image/libcommon_image.a")
    message(FATAL_ERROR "Prebuilt libs not found in STIQY_BUILD_DIR=${STIQY_BUILD_DIR}. Build StiQy first.")
endif()

option(STIQY_ENABLE_NVIDIA_OFA "Enable NVIDIA Optical Flow in SponsorGridTracker" ON)
set(STIQY_HAS_NVIDIA_OFA OFF)
set(NVIDIA_OPTICAL_FLOW_INCLUDE_DIR "")
if(STIQY_ENABLE_NVIDIA_OFA)
    find_path(NVIDIA_OPTICAL_FLOW_INCLUDE_DIR NAMES nvOpticalFlowCommon.h
        HINTS ${STIQY_ROOT}/third_party/NVIDIAOpticalFlowSDK /opt/NVIDIAOpticalFlowSDK
        PATH_SUFFIXES include NvOFInterface)
    if(NVIDIA_OPTICAL_FLOW_INCLUDE_DIR)
        set(STIQY_HAS_NVIDIA_OFA ON)
    endif()
endif()

find_package(CUDAToolkit REQUIRED)
set(CUDA_TOOLKIT_ROOT_DIR "${CUDAToolkit_ROOT}" CACHE PATH "CUDA root for OpenCV" FORCE)
set(ENV{CUDA_PATH} "${CUDAToolkit_ROOT}")

find_package(OpenCV REQUIRED
    core highgui imgproc imgcodecs video videoio
    features2d calib3d
    cudaimgproc cudawarping cudaoptflow cudaarithm cudafeatures2d
)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
# Match the Python version StiQy was built against (3.12).
set(Python_EXECUTABLE "/usr/bin/python3.12" CACHE FILEPATH "Python interpreter" FORCE)
find_package(Python 3.12 EXACT COMPONENTS Interpreter Development REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
pkg_search_module(GLEW REQUIRED glew)
pkg_search_module(GLFW REQUIRED glfw3)

add_subdirectory(${STIQY_ROOT}/third_party/pybind11 ${CMAKE_BINARY_DIR}/pybind11)

include(cmake/StiQyPrebuilt.cmake)
add_subdirectory(app)
