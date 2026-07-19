# Changelog

All notable changes to this project will be documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project intends to use
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Cross-platform SDK-independent tests and GitHub Actions CI.
- Public documentation, licensing, security, conduct, and contribution guidance.
- Explicit `--force` capture overwrite authorization with SDK-independent file-safety coverage.
- A private security-advisory route with a detail-free public contact fallback.

### Changed

- Refactored shared SDRplay device lifecycle management.
- Moved IQ file writes from the stream callback to a bounded writer queue.
- Improved argument validation, shutdown handling, metadata, and portable builds.
- Prevented startup signals observed before initialization from starting the receiver.
- Counted accepted queued blocks discarded after writer failure as dropped blocks.
- Improved architecture-aware Windows SDK discovery and documented explicit CMake overrides.

## [0.1.0] - Unreleased

Initial public-release preparation for the RSP1B probe and IQ capture tools.
