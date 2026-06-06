# AGENTS.md

## Project Purpose

RpiCamTs is a standalone Raspberry Pi camera recording daemon.

The intended future pipeline is:

Camera Capture -> H264 Encoding -> MPEG-TS -> Storage

The project should eventually support variable frame rate recording and reliable local storage.

The current implementation is a camera and H264 pipeline verification
prototype.

## Development Rules

- Keep the project simple and readable.
- Prefer clear C++ code over clever abstractions.
- Do not implement the full recording pipeline until explicitly requested.
- Do not move code from the submodule unless requested.
- Treat `third_party/mediamtx-rpicamera-fork` as an external reference/prototype source.
- Keep build output under `build/`.
- Keep final generated binaries under `dst/`.
- Avoid committing generated build artifacts.
- Prefer small, reviewable changes.
- Update `README.md` when project behavior changes.

## Current Source Structure

The pipeline verification code is split into small modules under `src/`. It
should confirm:

Camera -> Capture -> H264 Encoding -> Frame Info Dump

Keep module boundaries simple:

- process launch logic belongs in `mtxrpicam_process.*`
- pipe packet format belongs in `packet_io.*`
- camera parameter generation belongs in `camera_params.*`
- H264 inspection belongs in `h264_inspector.*`
- frame statistics belongs in `stats.*`
- app orchestration belongs in `app.*`

Do not implement TS recording until explicitly requested.

## Configuration Rule

RpiCamTs supports loading camera parameters from a flat YAML file.

The config keys should match the parameters accepted by the backend
`parameters_unserialize` function.

Keep the config format simple:

```yaml
Key: Value
```

Do not add a third-party YAML dependency unless explicitly requested.

Local-only RpiCamTs options, such as `MtxRpiCamPath`, must not be serialized
and sent to the backend camera parameter parser.
