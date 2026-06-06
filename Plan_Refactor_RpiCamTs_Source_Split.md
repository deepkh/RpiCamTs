# Plan: Refactor RpiCamTs Single Source File into Reasonable src/*.cpp Modules

## Goal

Refactor the current single-file implementation:

```text
src/rpi_cam_ts.cpp
```

into multiple reasonable source/header files under `src/`.

This stage is a **refactor only**.

The runtime behavior should remain the same:

```text
Camera -> Capture -> H264 Encoding -> Pipe Reader -> Frame/NALU/FPS Info Dump
```

Do **not** implement MPEG-TS recording in this stage.

The target binary should remain:

```text
RpiCamTs
```

The binary should still be generated under:

```text
build/RpiCamTs
dst/RpiCamTs
```

---

## Important Rule

This task should not change behavior.

Refactor only.

Do not add new major features.

Do not implement:

- MPEG-TS muxing
- TS file writing
- storage layout
- segment rotation
- retention policy
- systemd service
- config file parser
- multi-camera support

---

## Current Problem

The current `src/rpi_cam_ts.cpp` contains too many responsibilities in one file:

- main function
- signal handling
- path resolution
- environment variable handling
- camera parameter building
- pipe setup
- child process launch
- packet read/write helpers
- H264 NALU inspection
- frame statistics
- main capture loop

This makes the file hard to maintain before adding TS recording later.

---

## Target Source Layout

Refactor into this structure:

```text
src/
  rpi_cam_ts.cpp

  app.h
  app.cpp

  camera_params.h
  camera_params.cpp

  h264_inspector.h
  h264_inspector.cpp

  mtxrpicam_process.h
  mtxrpicam_process.cpp

  packet_io.h
  packet_io.cpp

  path_utils.h
  path_utils.cpp

  signal_handler.h
  signal_handler.cpp

  stats.h
  stats.cpp
```

Keep the entry source file:

```text
src/rpi_cam_ts.cpp
```

Do **not** reintroduce `src/main.cpp` in this stage.

`src/rpi_cam_ts.cpp` should become a small entry point only.

---

## Responsibility Split

### 1. src/rpi_cam_ts.cpp

Keep this file small.

Responsibilities:

- parse optional command line argument for custom `mtxrpicam` path
- create `RpiCamTsApp`
- run the app
- return process exit code

Example shape:

```cpp
#include "app.h"

int main(int argc, char** argv) {
    RpiCamTsOptions options;

    if (argc >= 2) {
        options.mtxrpicam_path = argv[1];
    }

    RpiCamTsApp app(options);
    return app.run();
}
```

---

### 2. src/app.h / src/app.cpp

Main orchestration layer.

Responsibilities:

- install signal handlers
- resolve paths
- create pipes
- launch `mtxrpicam`
- send config packet
- run video packet reading loop
- update frame statistics
- print frame information
- send end packet on shutdown
- close file descriptors
- wait for child process

Create:

```cpp
struct RpiCamTsOptions {
    std::string mtxrpicam_path;
};

class RpiCamTsApp {
public:
    explicit RpiCamTsApp(RpiCamTsOptions options);
    int run();

private:
    RpiCamTsOptions options_;
};
```

The behavior should be moved from the current main loop into `RpiCamTsApp::run()`.

Keep cleanup simple and reliable.

---

### 3. src/camera_params.h / src/camera_params.cpp

Move camera parameter generation here.

Responsibilities:

- build parameter string used by `mtxrpicam`
- read environment variables
- apply defaults
- base64 encode text parameters when needed
- resolve tuning file path

Expose:

```cpp
std::string build_camera_parameters();
```

Move these helpers here if they currently exist in `rpi_cam_ts.cpp`:

```text
getenv_or_default()
getenv_or_default_int()
getenv_or_default_double()
base64_encode()
default_tuning_file()
```

Keep the same default values as the current implementation.

Important defaults should remain:

```text
Width=2304
Height=1296
MinFPS=5.0
MaxFPS=60.0
Bitrate=10000000
IDRPeriod=60
Codec=auto
HardwareH264Profile=main
HardwareH264Level=4.1
```

Do not change the parameter format.

It should remain:

```text
Key:Value Key:Value Key:Value
```

---

### 4. src/h264_inspector.h / src/h264_inspector.cpp

Move H264 NALU inspection logic here.

Responsibilities:

- detect H264 start code
- parse NALU type
- identify SPS/PPS/SEI/AUD
- identify slice type when possible
- return a readable summary string

Expose:

```cpp
std::string collect_h264_nalu_info(const uint8_t* data, size_t size);
```

Move local bit reader helpers here too.

Keep implementation private inside `.cpp` where possible.

Only expose the public function in the header.

Expected result examples:

```text
SPS PPS I
P
SEI SPS PPS I
AUD P
```

---

### 5. src/mtxrpicam_process.h / src/mtxrpicam_process.cpp

Move child process launch and environment setup here.

Responsibilities:

- locate `mtxrpicam`
- setup child environment
- setup `PIPE_CONF_FD`
- setup `PIPE_VIDEO_FD`
- setup `LD_LIBRARY_PATH`
- setup `LIBPISP_BE_CONFIG_FILE`
- fork and exec `mtxrpicam`
- wait for child process

Create:

```cpp
struct MtxRpiCamProcessOptions {
    std::string mtxrpicam_path;
    int conf_read_fd = -1;
    int video_write_fd = -1;
};

class MtxRpiCamProcess {
public:
    explicit MtxRpiCamProcess(MtxRpiCamProcessOptions options);

    bool start();
    void request_stop();
    int wait();

    pid_t pid() const;

private:
    MtxRpiCamProcessOptions options_;
    pid_t pid_ = -1;
};
```

Also move these helpers here or to `path_utils` if better:

```text
prepend_ld_library_path()
set_libpisp_config_file()
```

The child process should still execute `mtxrpicam` exactly as before.

---

### 6. src/packet_io.h / src/packet_io.cpp

Move pipe packet read/write logic here.

Responsibilities:

- robust read
- robust write
- write config packet
- write end packet
- read video packet
- parse packet size/kind/timestamp/payload

Create:

```cpp
struct VideoPacket {
    char kind = '\0';
    uint64_t timestamp = 0;
    std::vector<uint8_t> payload;
    bool has_timestamp = false;
};

bool write_config_packet(int fd, const std::string& parameters);
bool write_end_packet(int fd);
bool read_video_packet(int fd, VideoPacket& packet);
```

Use the same packet format as the current implementation.

Config write format:

```text
uint32_t size
char kind = 'c'
payload
```

End packet:

```text
uint32_t size
char kind = 'e'
empty payload
```

Video packet read format:

```text
uint32_t size
char kind
optional uint64_t timestamp
payload
```

For `kind == 'd'` or `kind == 's'`, read timestamp if enough bytes exist.

Do not change packet compatibility with `mtxrpicam`.

---

### 7. src/path_utils.h / src/path_utils.cpp

Move path and file utility helpers here.

Responsibilities:

- check file exists
- check executable exists
- convert relative path to absolute path
- resolve project-relative paths
- locate default `mtxrpicam`
- locate tuning/config files if not kept in other modules

Expose helpers such as:

```cpp
bool file_exists(const std::string& path);
bool is_executable(const std::string& path);
std::string absolute_path(const std::string& path);
std::string default_mtxrpicam_path();
```

`default_mtxrpicam_path()` should still try:

```text
./third_party/mediamtx-rpicamera-fork/mtxrpicam
./third_party/mediamtx-rpicamera-fork/build/mtxrpicam
./mtxrpicam
./build/mtxrpicam
```

---

### 8. src/signal_handler.h / src/signal_handler.cpp

Move signal handling here.

Responsibilities:

- install handlers for `SIGINT`, `SIGTERM`, and `SIGPIPE`
- expose stop flag query
- ignore `SIGPIPE`
- keep signal-safe handler simple

Expose:

```cpp
void install_signal_handlers();
bool stop_requested();
void request_stop();
```

Behavior:

- `SIGINT` sets stop flag
- `SIGTERM` sets stop flag
- `SIGPIPE` is ignored

Use `std::atomic_bool` or `volatile sig_atomic_t`.

Prefer signal-safe implementation.

---

### 9. src/stats.h / src/stats.cpp

Move rolling frame statistics here.

Responsibilities:

- track frame count
- track timestamp diff
- compute instant FPS
- compute average FPS
- compute min/max FPS
- compute average/min/max diff
- keep approximately last 2 seconds of samples

Create:

```cpp
struct FrameStatsSnapshot {
    uint64_t frame_count = 0;

    double timestamp_sec = 0.0;
    double diff_sec = 0.0;

    double instant_fps = 0.0;

    double avg_fps = 0.0;
    double min_fps = 0.0;
    double max_fps = 0.0;

    double avg_diff = 0.0;
    double min_diff = 0.0;
    double max_diff = 0.0;
};

class FrameStats {
public:
    FrameStatsSnapshot update(uint64_t timestamp_us);
};
```

Keep output formatting in `app.cpp`, not inside `FrameStats`.

---

## CMakeLists.txt Update

Update `CMakeLists.txt` to build all source files.

Target should remain:

```cmake
add_executable(RpiCamTs
    src/rpi_cam_ts.cpp
    src/app.cpp
    src/camera_params.cpp
    src/h264_inspector.cpp
    src/mtxrpicam_process.cpp
    src/packet_io.cpp
    src/path_utils.cpp
    src/signal_handler.cpp
    src/stats.cpp
)
```

Add include path:

```cmake
target_include_directories(RpiCamTs PRIVATE
    src
)
```

Keep C++17:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

Keep warnings:

```cmake
target_compile_options(RpiCamTs PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
```

Do not add external dependencies.

---

## build.sh Update

`build.sh` should continue to work without behavior change.

Make sure it still checks and copies:

```text
build/RpiCamTs
dst/RpiCamTs
```

Do not rename the binary.

Do not change the submodule build logic unless needed for compatibility.

---

## README.md Update

Update README only lightly.

Mention that the implementation has been split into modules.

Suggested text:

```markdown
## Source Layout

The current pipeline verification implementation is split into several small modules:

- `rpi_cam_ts.cpp` - program entry point
- `app.cpp` - main pipeline orchestration
- `camera_params.cpp` - camera parameter generation
- `mtxrpicam_process.cpp` - backend process launch
- `packet_io.cpp` - pipe packet read/write helpers
- `h264_inspector.cpp` - H264 NALU information extraction
- `stats.cpp` - frame statistics
- `signal_handler.cpp` - shutdown handling
- `path_utils.cpp` - path helpers
```

Keep the current status clear:

```text
MPEG-TS recording is not implemented yet.
```

---

## AGENTS.md Update

Add a note that the previous single-file prototype has now been split.

Suggested text:

```markdown
## Current Source Structure

The pipeline verification code is now split into small modules under `src/`.

Keep module boundaries simple:

- process launch logic belongs in `mtxrpicam_process.*`
- pipe packet format belongs in `packet_io.*`
- camera parameter generation belongs in `camera_params.*`
- H264 inspection belongs in `h264_inspector.*`
- frame statistics belongs in `stats.*`
- app orchestration belongs in `app.*`

Do not implement MPEG-TS recording until explicitly requested.
```

---

## Refactor Rules

While refactoring:

1. Preserve current runtime behavior.
2. Preserve current command line behavior.
3. Preserve current environment variable behavior.
4. Preserve current packet format.
5. Preserve current frame info output as much as possible.
6. Preserve graceful Ctrl+C shutdown.
7. Avoid unnecessary architecture.
8. Avoid adding dependencies.
9. Keep headers minimal.
10. Keep helper functions private in `.cpp` files when possible.

---

## Suggested Refactor Order

### Step 1: Create utility modules

Create first:

```text
path_utils.h/.cpp
signal_handler.h/.cpp
packet_io.h/.cpp
```

Move simple helpers first.

Build after this step.

---

### Step 2: Move H264 inspection

Create:

```text
h264_inspector.h/.cpp
```

Move all NALU and bit-reader related logic.

Build after this step.

---

### Step 3: Move frame statistics

Create:

```text
stats.h/.cpp
```

Move rolling FPS/diff logic.

Build after this step.

---

### Step 4: Move camera parameter builder

Create:

```text
camera_params.h/.cpp
```

Move environment/default/base64/tuning parameter logic.

Build after this step.

---

### Step 5: Move mtxrpicam process management

Create:

```text
mtxrpicam_process.h/.cpp
```

Move fork/exec/environment setup logic.

Build after this step.

---

### Step 6: Create app orchestration

Create:

```text
app.h/.cpp
```

Move the main loop into `RpiCamTsApp::run()`.

Leave `src/rpi_cam_ts.cpp` as a tiny entry point.

Build after this step.

---

## Validation

After refactor, run:

```bash
./build.sh
```

Verify:

```bash
ls -l build/RpiCamTs
ls -l dst/RpiCamTs
```

Run:

```bash
./dst/RpiCamTs
```

Expected behavior:

- `mtxrpicam` launches as before
- camera config is sent as before
- H264 packets are read as before
- frame/FPS/timestamp/NALU information is printed as before
- Ctrl+C exits cleanly as before

Also test optional custom backend path:

```bash
./dst/RpiCamTs /path/to/mtxrpicam
```

---

## Expected Git Changes

Expected files:

```text
modified: CMakeLists.txt
modified: README.md
modified: AGENTS.md
modified: src/rpi_cam_ts.cpp

new file: src/app.h
new file: src/app.cpp
new file: src/camera_params.h
new file: src/camera_params.cpp
new file: src/h264_inspector.h
new file: src/h264_inspector.cpp
new file: src/mtxrpicam_process.h
new file: src/mtxrpicam_process.cpp
new file: src/packet_io.h
new file: src/packet_io.cpp
new file: src/path_utils.h
new file: src/path_utils.cpp
new file: src/signal_handler.h
new file: src/signal_handler.cpp
new file: src/stats.h
new file: src/stats.cpp
```

---

## Final Acceptance Criteria

This task is complete when:

1. `src/rpi_cam_ts.cpp` is small and only contains the entry point.
2. The previous single-file logic is split into reasonable modules.
3. `./build.sh` succeeds.
4. `build/RpiCamTs` exists.
5. `dst/RpiCamTs` exists.
6. Runtime behavior is equivalent to the previous stage.
7. No MPEG-TS recording logic is implemented.
8. README and AGENTS are updated to describe the new source layout.

---

## Source Naming Recommendation

For this refactor stage, keep `src/rpi_cam_ts.cpp` as the entry point instead of bringing back `src/main.cpp`. This keeps the binary identity and source naming consistent with the current `RpiCamTs` stage.
