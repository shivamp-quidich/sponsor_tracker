# Import prebuilt StiQy static libraries (built via StiQy_2.0/build-release).

function(import_stiqy_static name rel_path)
    add_library(${name} STATIC IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES
        IMPORTED_LOCATION "${STIQY_BUILD_DIR}/${rel_path}"
    )
endfunction()

set(STIQY_INCLUDE_DIRS
    ${STIQY_ROOT}/include
    ${STIQY_ROOT}/modules/sponsor_grid_tracker/include
    ${STIQY_ROOT}/common/logger/include
    ${STIQY_ROOT}/common/image/include
    ${STIQY_ROOT}/common/opengl/include
    ${STIQY_ROOT}/common/reinit_sidecar/include
    ${STIQY_ROOT}/third_party/pybind11/include
    ${Python_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

import_stiqy_static(logger "common/logger/liblogger.a")
import_stiqy_static(common_image "common/image/libcommon_image.a")
import_stiqy_static(opengl_context "common/opengl/libopengl_context.a")
import_stiqy_static(reinit_sidecar "common/reinit_sidecar/libreinit_sidecar.a")

# Rebuild tracker against the local OpenCV (prebuilt .a targets OpenCV 4.10 ABI).
add_library(sponsor_grid_tracker STATIC
    ${STIQY_ROOT}/modules/sponsor_grid_tracker/src/SponsorGridTracker.cpp
)
target_include_directories(sponsor_grid_tracker
    PUBLIC
        ${STIQY_ROOT}/modules/sponsor_grid_tracker/include
        ${OpenCV_INCLUDE_DIRS}
        ${STIQY_ROOT}/third_party/pybind11/include
        ${Python_INCLUDE_DIRS}
    PRIVATE
        ${STIQY_INCLUDE_DIRS}
)
if(STIQY_HAS_NVIDIA_OFA)
    target_include_directories(sponsor_grid_tracker PRIVATE ${NVIDIA_OPTICAL_FLOW_INCLUDE_DIR})
    target_compile_definitions(sponsor_grid_tracker PRIVATE STIQY_ENABLE_NVIDIA_OFA=1)
else()
    target_compile_definitions(sponsor_grid_tracker PRIVATE STIQY_ENABLE_NVIDIA_OFA=0)
endif()
target_link_libraries(sponsor_grid_tracker PUBLIC pybind11::embed)
target_compile_features(sponsor_grid_tracker PUBLIC cxx_std_17)
set_target_properties(sponsor_grid_tracker PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(stiqy_sponsor_bundle INTERFACE)
target_include_directories(stiqy_sponsor_bundle INTERFACE ${STIQY_INCLUDE_DIRS})
target_link_libraries(stiqy_sponsor_bundle INTERFACE
    sponsor_grid_tracker
    common_image
    opengl_context
    reinit_sidecar
    logger
    spdlog::spdlog
    pybind11::embed
    CUDA::cudart
    CUDA::cudart_static
    OpenGL::GL
    ${GLEW_LIBRARIES}
    glfw
    ${Python_LIBRARIES}
    pthread
    dl
)
