cmake_minimum_required(VERSION 3.16)

project(RpiCamTs
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(RpiCamTs
    src/rpi_cam_ts.cpp
    src/app.cpp
    src/camera_params.cpp
    src/config_loader.cpp
    src/default_config.cpp
    src/h264_inspector.cpp
    src/mtxrpicam_process.cpp
    src/packet_io.cpp
    src/path_utils.cpp
    src/signal_handler.cpp
    src/stats.cpp
)

target_include_directories(RpiCamTs PRIVATE
    src
)

target_compile_options(RpiCamTs PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
