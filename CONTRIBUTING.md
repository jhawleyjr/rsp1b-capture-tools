# Contributing

Thank you for helping improve RSP1B Capture Tools. Keep pull requests focused, explain the problem
being solved, and avoid unrelated reformatting.

## Development setup

The project requires CMake 3.16 or newer and a C++17 compiler. Portable logic can be built and tested
without the proprietary SDRplay SDK:

```sh
cmake -S . -B build-ci \
  -DRSP1B_BUILD_TOOLS=OFF \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ci --config Release --parallel
ctest --test-dir build-ci -C Release --output-on-failure
```

To compile the hardware tools, install the official SDRplay API 3.14 or newer and configure with
`RSP1B_BUILD_TOOLS=ON`. If CMake cannot locate it, set `SDRPLAY_API_ROOT` to the installation prefix.
The reliable fallback is to set `SDRPLAY_API_INCLUDE_DIR` to the directory containing
`sdrplay_api.h` and `SDRPLAY_API_LIBRARY` to the full path of the architecture-matching library.

## Style and tests

- Use four-space indentation and follow `.clang-format` and `.editorconfig`.
- Add or update SDK-independent tests for portable logic.
- Run `git diff --check`, the SDK-free build, and CTest before submitting.
- Add overwrite-safety and failure-accounting coverage when changing output handling; tests must not
  replace pre-existing fixtures without explicit authorization.
- If you have the official SDK, compile the hardware tools without assuming that CI can do so.
- Do not add SDRplay headers, libraries, drivers, services, installers, documentation, or other
  proprietary artifacts.

## Hardware testing

Hardware reports should identify the OS, compiler, CMake version, SDRplay API version, genuine RSP1B
receiver, exact command, and whether Bias-T was enabled. Redact receiver serial numbers and personal
paths from logs.

Never enable Bias-T unless the connected antenna, preamplifier, cabling, and other equipment are
designed for the supplied DC voltage. Contributors are responsible for their own hardware safety.
