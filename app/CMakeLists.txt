add_executable(sponsor_tracker
    main.cpp
    frame_utils.cpp
    tracking_params_stub.cpp
    config_loader.cpp
)

target_include_directories(sponsor_tracker PRIVATE
    ${STIQY_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link static archives and OpenCV in one group so CUDA module symbols resolve.
target_link_libraries(sponsor_tracker PRIVATE
    yaml-cpp
    pybind11::embed
    CUDA::cudart
    CUDA::cudart_static
    OpenGL::GL
    ${GLEW_LIBRARIES}
    glfw
    ${Python_LIBRARIES}
    pthread
    dl
    -Wl,--start-group
    sponsor_grid_tracker
    common_image
    opengl_context
    reinit_sidecar
    logger
    ${OpenCV_LIBS}
    -Wl,--end-group
    spdlog::spdlog
)

target_compile_features(sponsor_tracker PRIVATE cxx_std_17)
