# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- HIP: the `sensors` program's `WriteDetectors` kernel compiles with hipRTC from ROCm 7.0 on, so the first
  render through a `hip:N` adapter no longer fails there. The runtime-compiler test compiles
  `WriteDetectors` and `WriteDistortions`.

## [0.1.0] - 2026-11-12

### Added

- First public release.
