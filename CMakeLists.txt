cmake_minimum_required(VERSION 3.16)

project(RpiCamTs
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
)

add_executable(RpiCamTs
    src/rpi_cam_ts.cpp
    src/app.cpp
    src/camera_params.cpp
    src/config_loader.cpp
    src/default_config.cpp
    src/h264_file_writer.cpp
    src/h264_inspector.cpp
    src/mtxrpicam_process.cpp
    src/packet_io.cpp
    src/path_utils.cpp
    src/signal_handler.cpp
    src/stats.cpp
    src/ts_muxer_ffmpeg.cpp
)

target_include_directories(RpiCamTs PRIVATE
    src
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(RpiCamTs PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(RpiCamTs PRIVATE
    ${FFMPEG_LIBRARIES}
)

target_compile_options(RpiCamTs PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${FFMPEG_CFLAGS_OTHER}
)
