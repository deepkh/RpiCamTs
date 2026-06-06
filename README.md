# PiCamTS

PiCamTS is a lightweight Raspberry Pi camera recording daemon project.

The long-term goal is to record camera video using the following pipeline:

```text
Camera Capture -> H264 Encoding -> MPEG-TS -> Storage
```

PiCamTS is planned to support variable frame rate recording and local MPEG-TS storage.

## Current Status

The project currently builds `RpiCamTs`, a pipeline verification program.

It launches the `mtxrpicam` backend from the
`mediamtx-rpicamera-fork` submodule, sends camera parameters through the
config pipe, reads H264 encoded video packets, and prints frame, FPS,
timestamp, packet size, and NALU information.

MPEG-TS recording is not implemented yet.

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

To use a custom backend path:

```bash
./dst/RpiCamTs /path/to/mtxrpicam
```

On a Raspberry Pi camera system, the program launches the backend and prints
one line per encoded H264 frame. Press `Ctrl+C` to stop it cleanly.

## Roadmap

Future stages may include:

- Add MPEG-TS writer
- Add variable frame rate timestamp handling
- Add segment recording
- Add storage retention policy
- Add systemd daemon support
