# AGENTS.md

## Project Purpose

PiCamTS is a standalone Raspberry Pi camera recording daemon.

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

## Current Stage

The project currently contains a single source file:

```text
src/rpi_cam_ts.cpp
```

This file is a pipeline verification prototype. It should confirm:

Camera -> Capture -> H264 Encoding -> Frame Info Dump

Do not implement TS recording until explicitly requested.
