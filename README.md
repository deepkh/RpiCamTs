# RpiCamTs

RpiCamTs is a lightweight Raspberry Pi camera recording daemon project.

The long-term goal is to record camera video using the following pipeline:

```text
Camera Capture -> H264 Encoding -> MPEG-TS -> Storage
```

RpiCamTs preserves camera timestamps when writing local MPEG-TS output.

## Current Status

The project currently builds `RpiCamTs`, a pipeline verification program.

It launches the `mtxrpicam` backend from the
`mediamtx-rpicamera-fork` submodule, sends camera parameters through the
config pipe, reads H264 encoded video packets, and prints frame, FPS,
timestamp, packet size, and NALU information. It also writes the encoded H264
payload to either a raw `.264`/`.h264` file or an MPEG-TS `.ts` file.

## Source Layout

The current pipeline verification implementation is split into several small
modules:

- `rpi_cam_ts.cpp` - program entry point
- `app.cpp` - main pipeline orchestration
- `camera_params.cpp` - camera parameter generation
- `config_loader.cpp` - flat YAML configuration loading
- `default_config.cpp` - default YAML configuration generation
- `h264_file_writer.cpp` - raw H264 output
- `ts_muxer_ffmpeg.cpp` - FFmpeg MPEG-TS output
- `mtxrpicam_process.cpp` - backend process launch
- `packet_io.cpp` - pipe packet read/write helpers
- `h264_inspector.cpp` - H264 NALU information extraction
- `stats.cpp` - frame statistics
- `signal_handler.cpp` - shutdown handling
- `path_utils.cpp` - path helpers

## Prototype Source

The early prototype code is based on work from:

```text
https://github.com/deepkh/mediamtx-rpicamera-fork
```

The submodule is checked out from branch:

```text
v2.5.5_fake_stream_reader
```

Submodule location:

```text
third_party/mediamtx-rpicamera-fork
```

## Dependencies

RpiCamTs uses FFmpeg libraries for MPEG-TS output. On Debian or Raspberry Pi
OS, install the development packages with:

```bash
sudo apt install pkg-config libavformat-dev libavcodec-dev libavutil-dev
```

CMake stops during configuration with a dependency error if these libraries
cannot be found.

## Build

Run:

```bash
./build.sh
```

The script will:

1. Initialize/update the submodule
2. Build `mediamtx-rpicamera-fork` under `build/mediamtx-rpicamera-fork/`
3. Generate CMake build files under `build/`
4. Build `RpiCamTs`
5. Copy the final binary to `dst/RpiCamTs`
6. Copy shared libraries produced by the prototype build to `dst/`

## Output

After building, the binary should be available at:

```text
build/RpiCamTs
dst/RpiCamTs
```

The prototype build outputs remain under:

```text
build/mediamtx-rpicamera-fork/
```

Any installed `*.so` and versioned `*.so.*` files from that build are also
copied directly into `dst/`.

Run it:

```bash
./dst/RpiCamTs
```

On a Raspberry Pi camera system, the program launches the backend, writes the
raw H264 stream to `video.264`, and prints one line per encoded H264 frame.
Press `Ctrl+C` to stop it cleanly.

## Raw H264 Output

Run with the default config and default output:

```bash
./dst/RpiCamTs
```

The default output file is `video.264`. To select another output file:

```bash
./dst/RpiCamTs output.264
```

Use a custom config and output together:

```bash
./dst/RpiCamTs camera.yml output.264
```

The output file is overwritten if it already exists.

The `.h264` extension is also accepted for raw H264 output.

## MPEG-TS Output

RpiCamTs can mux the camera's H264 stream into MPEG-TS using FFmpeg libraries.
Run with the default config:

```bash
./dst/RpiCamTs output.ts
```

Run with a custom config:

```bash
./dst/RpiCamTs camera.yml output.ts
```

Camera timestamps from the video pipe are normalized at the first frame and
used as packet PTS and DTS values. No fixed frame duration is generated, so
variable frame rate timing is preserved as supplied by the camera. Separate
SPS/PPS encoder output is joined to its following video frame so each muxed
packet represents one H264 access unit. If two frame timestamps resolve to the
same 90 kHz MPEG-TS tick, the later timestamp is advanced by one tick to keep
DTS strictly increasing.

Usage:

```text
RpiCamTs [output.264|output.h264|output.ts]
RpiCamTs [config.yml] [output.264|output.h264|output.ts]
RpiCamTs --generate-default-config <output.yml>
```

## Configuration File

RpiCamTs can load camera parameters from a flat YAML config file. Run with a
custom config:

```bash
./dst/RpiCamTs camera_test.yml
```

When no config argument is provided, RpiCamTs tries to load `RpiCamTs.yml`.
If that default file does not exist, it warns and continues with built-in
defaults. An explicitly requested missing or invalid config is an error.

Start from the example config:

```bash
cp examples/RpiCamTs.yml RpiCamTs.yml
./dst/RpiCamTs
```

The supported format is one `Key: Value` pair per line, with optional quotes,
blank lines, and `#` comments. Camera keys match the backend's
`parameters_unserialize` function. Config values override environment
variables, which override built-in defaults.

`MtxRpiCamPath` is an optional RpiCamTs-only setting for selecting the backend
executable. It is not sent to the camera parameter parser.

## Generate Default Config

Generate a default YAML config file and exit without starting the camera
pipeline:

```bash
./dst/RpiCamTs --generate-default-config RpiCamTs.yml
```

Then run with the default filename:

```bash
./dst/RpiCamTs
```

Custom output names are also supported:

```bash
./dst/RpiCamTs --generate-default-config test_camera.yml
./dst/RpiCamTs test_camera.yml
```

For safety, RpiCamTs will not overwrite an existing config file.

## Roadmap

Future stages may include:

- Add segment recording
- Add storage retention policy
- Add systemd daemon support
