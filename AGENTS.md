# AGENTS.md

## Project Purpose

PiCamTS is a standalone Raspberry Pi camera recording daemon.

The intended future pipeline is:

Camera Capture -> H264 Encoding -> MPEG-TS -> Storage

The project should eventually support variable frame rate recording and reliable local storage.

The current implementation is only an initial project skeleton.

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

This stage only creates:

- git repository initialization
- mediamtx-rpicamera-fork submodule
- README
- AGENTS.md
- minimal C++ entry point
- CMake build system
- build.sh helper script
