# PiCamTS

PiCamTS is a lightweight Raspberry Pi camera recording daemon project.

The long-term goal is to record camera video using the following pipeline:

```text
Camera Capture -> H264 Encoding -> MPEG-TS -> Storage
```

PiCamTS is planned to support variable frame rate recording and local MPEG-TS storage.

## Current Status

This repository is currently only a project skeleton.

The real recording pipeline is not implemented yet.

Current features:

- Basic C++ entry point
- CMake build setup
- `build.sh` helper script
- `mediamtx-rpicamera-fork` added as a git submodule and built as prototype code

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
4. Build PiCamTS
5. Copy the final binary to `dst/PiCamTS`
6. Copy shared libraries produced by the prototype build to `dst/`

## Output

After building, the binary should be available at:

```text
dst/PiCamTS
```

The prototype build outputs remain under:

```text
build/mediamtx-rpicamera-fork/
```

Any installed `*.so` and versioned `*.so.*` files from that build are also
copied directly into `dst/`.

Run it:

```bash
./dst/PiCamTS
```

Expected output:

```text
Hello from PiCamTS
```

## Roadmap

Future stages may include:

- Extract camera packet reader logic
- Add H264 packet handling
- Add MPEG-TS writer
- Add variable frame rate timestamp handling
- Add segment recording
- Add storage retention policy
- Add systemd daemon support
