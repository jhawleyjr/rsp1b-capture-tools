# RSP1B Capture Tools

[![Portable CI](https://github.com/jhawleyjr/rsp1b-capture-tools/actions/workflows/ci.yml/badge.svg)](https://github.com/jhawleyjr/rsp1b-capture-tools/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)

RSP1B Capture Tools provides command-line IQ capture and diagnostic tools specifically for genuine
SDRplay RSP1B receivers. The project is preparing for its initial `0.1.0` public release and should be
treated as early-stage software, especially around hardware and high-throughput disk behavior.

The repository contains original client code only. It does not distribute the proprietary SDRplay
API, drivers, service or daemon, headers, libraries, installers, or other SDK artifacts.

## Features

- `rsp1b_probe` configures a genuine RSP1B, streams for about one second, and reports callback,
  sample-range, reset, and event statistics.
- `rsp1b_capture` writes raw interleaved signed 16-bit IQ data and a companion metadata file.
- Bias-T is disabled by default and explicitly requested off during normal-thread shutdown.
- `SIGINT` and `SIGTERM` request graceful shutdown where supported.
- A bounded producer/consumer queue keeps synchronous disk I/O out of the SDRplay stream callback.
- Queue overflow, writer failure, device removal, resets, overload acknowledgements, and partial
  captures are reported rather than silently ignored.
- Portable argument parsing, timestamps, metadata, and writer logic are tested without the SDK.

## Supported hardware and software

Only genuine SDRplay RSP1B receivers are supported. The tools deliberately reject other receiver
models.

The build supports little-endian Linux, macOS, and Windows targets with a C++17 compiler and an
official SDRplay API installation. SDRplay added RSP1B support in API 3.14, which is the minimum
accepted version. The current code has been compiled on macOS against API 3.15 headers and libraries;
the runtime API version must match the version used at compile time.

Portable CI runs on Ubuntu, macOS, and Windows, but it does not download, compile, or exercise the
proprietary SDRplay adapter. Real SDK integration and receiver behavior require local verification.

## Prerequisites

- CMake 3.16 or newer
- A C++17 compiler:
  - Apple Clang or Clang on macOS
  - GCC or Clang on Linux
  - Visual Studio 2022 or another suitable MSVC installation on Windows
- Threads supported by the host toolchain
- For hardware tools, the official [SDRplay API](https://www.sdrplay.com/api/) 3.14 or newer,
  including its service or daemon and development headers and libraries
- A genuine SDRplay RSP1B receiver
- Sufficient sustained disk throughput for the selected sample rate

## Build

`RSP1B_BUILD_TOOLS` defaults to `ON`. CMake searches normal platform locations and accepts either the
CMake cache variable or environment variable `SDRPLAY_API_ROOT` as an installation-prefix hint.

### macOS

```sh
cmake -S . -B build \
  -DRSP1B_BUILD_TOOLS=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDRPLAY_API_ROOT=/usr/local
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Adjust `SDRPLAY_API_ROOT` if the official installer used another prefix.

### Linux

```sh
export SDRPLAY_API_ROOT=/usr/local
cmake -S . -B build \
  -DRSP1B_BUILD_TOOLS=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ensure the SDRplay API daemon is installed and running according to SDRplay's instructions. Change
the prefix if the API is installed elsewhere.

### Windows

From a Visual Studio developer PowerShell prompt:

```powershell
cmake -S . -B build `
  -DRSP1B_BUILD_TOOLS=ON `
  -DBUILD_TESTING=ON `
  -DSDRPLAY_API_ROOT=C:/path/to/SDRplay/API
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The matching SDRplay runtime DLL and service must be available through the official installation at
run time.

## Quick start

Compile first, connect a genuine RSP1B, and keep Bias-T off unless the attached equipment explicitly
requires and safely accepts DC power.

Probe the receiver without writing IQ data:

```sh
./build/rsp1b_probe
```

Capture two seconds using the default center frequency and sample rate:

```sh
./build/rsp1b_capture --duration 2 --out captures/test_2s.iq
```

On a multi-configuration Windows build, executables are normally under `build/Release/`.

## Capture command-line reference

```text
rsp1b_capture --duration SECONDS [--out captures/file.iq]
    [--bias-t 0|1] [--center HZ] [--sample-rate SPS]
```

| Option | Description | Default |
| --- | --- | --- |
| `--duration SECONDS` | Required finite capture duration greater than zero; decimals are accepted. | None |
| `--out PATH` | IQ output path. | `captures/rsp1b_capture_<timestamp>.iq` |
| `--bias-t 0|1` | Disable or explicitly enable antenna DC power. | `0` |
| `--center HZ` | Finite center frequency greater than zero; decimals are accepted. | `1575420000` |
| `--sample-rate SPS` | Finite sample rate greater than zero; decimals are accepted. | `5000000` |
| `--help`, `-h` | Print usage and exit successfully. | — |

Invalid, missing, non-finite, overflowing, underflowing, zero, and negative numeric values are
rejected before the IQ output is created or truncated.

Examples:

```sh
./build/rsp1b_capture --duration 10
./build/rsp1b_capture --duration 30 --center 1575420000 --sample-rate 5000000
./build/rsp1b_capture --duration 2.5 --out captures/gps_l1.iq
```

### Bias-T safety warning

`--bias-t 1` supplies DC power through the antenna port. Use it only with compatible antennas,
preamplifiers, cables, splitters, and test equipment. The program prints an explicit warning when it
is requested. Bias-T remains off by default, and normal shutdown makes a best-effort request to turn
it off before uninitializing the device.

## Graceful shutdown and limitations

On `Ctrl-C`, `SIGINT`, or `SIGTERM` where supported, the signal handler only sets a signal-safe stop
indicator. The application thread then stops streaming, requests Bias-T off, drains and closes the IQ
writer, writes partial-capture metadata when possible, releases the receiver, and closes the API. An
interrupted capture exits with a nonzero status.

This cleanup cannot be guaranteed after `SIGKILL`, power loss, process or operating-system crashes,
hardware failure, forced termination, or an SDK/service failure. Treat external Bias-T power safety
as a hardware responsibility rather than relying solely on process cleanup.

Device removal also requests normal-thread shutdown. SDRplay power-overload events are reported and
acknowledged with the selected device and event tuner as required by the API specification.

## Output format and disk space

The `.iq` file contains raw complex samples in this order:

```text
I0, Q0, I1, Q1, ...
```

Each component is a little-endian signed 16-bit integer, so each complex sample consumes four bytes.
The build rejects unsupported non-little-endian targets.

Approximate storage is:

```text
bytes = sample_rate_sps × duration_seconds × 4
```

At the default 5,000,000 samples/second, expect approximately 20 MB/second, 1.2 GB/minute, or
72 GB/hour using decimal units. Filesystem overhead and metadata are additional.

The stream callback interleaves samples and submits blocks to a bounded 64-block queue. A dedicated
thread writes accepted blocks in order. If the queue fills, the capture stops and fails instead of
silently discarding data. Sustained storage slower than the incoming data rate will therefore surface
as an error.

## Metadata

For `captures/example.iq`, the companion file is `captures/example.txt`. Existing keys describing the
receiver, serial number, frequency, sample rate, bandwidth, IF mode, Bias-T, notches, AGC, requested
duration, sample count, format, byte order, and local timestamp are retained.

Additional fields report:

- callbacks received;
- complex samples accepted and written;
- approximate expected samples;
- stream reset count;
- queue overflow and rejected-block counts;
- writer failure;
- graceful interruption;
- device removal;
- power-overload events and acknowledgement failures.

Metadata may describe a partial capture. Redact receiver serial numbers before sharing metadata or
logs publicly.

## Build and test without the SDRplay SDK

This is the configuration used by public CI:

```sh
cmake -S . -B build-ci \
  -DRSP1B_BUILD_TOOLS=OFF \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ci --config Release --parallel
ctest --test-dir build-ci -C Release --output-on-failure
```

With `RSP1B_BUILD_TOOLS=OFF`, CMake does not search for the SDRplay SDK and does not compile
`rsp1b_device.cpp`, `rsp1b_probe`, or `rsp1b_capture`. The tests cover argument parsing, metadata,
portable timestamps, block ordering, queue draining, writer failures, overflow, empty shutdown, and
closure behavior. They do not simulate the proprietary API or radio hardware.

The GitHub Actions workflow runs this portable configuration on `ubuntu-latest`, `macos-latest`, and
`windows-latest`. The stable aggregate check is named **CI success**.

## Troubleshooting

### CMake cannot find the SDRplay API

Install the official API and point `SDRPLAY_API_ROOT` at its prefix, which should contain the SDK
header and platform library beneath `include` and `lib`-style directories. For portable tests only,
configure with `-DRSP1B_BUILD_TOOLS=OFF`.

### API version mismatch

Rebuild with headers and libraries from the same official SDRplay API installation used by the
runtime service or daemon. RSP1B requires API 3.14 or newer.

### No RSP1B is found

Confirm that the connected receiver is a genuine SDRplay RSP1B, is not in use by another program,
and is recognized by the official SDRplay service, daemon, and driver. Other SDRplay models are not
selected by these tools.

### Queue overflow or writer failure

Use faster local storage, reduce the sample rate, avoid slow network or removable filesystems, and
check available disk space and filesystem limits. The program intentionally fails rather than hiding
lost blocks.

### Metadata exists for an incomplete capture

Inspect `interrupted`, `device_removed`, `writer_failure`, `queue_overflow_count`, accepted and written
sample counts, and terminal diagnostics. Partial metadata is intentional after graceful interruption
or a detected streaming failure.

## Architecture

- `src/rsp1b_device.*` owns SDRplay API open/close, version checks, selection, configuration,
  initialization, event handling, Bias-T shutdown, release, and cleanup through an RAII session.
- `src/rsp1b_capture.cpp` coordinates capture lifetime and the SDRplay stream callback.
- `src/rsp1b_probe.cpp` implements the short diagnostic stream.
- `src/capture_options.*`, `src/timestamp.*`, `src/metadata.*`, and `src/iq_writer.*` contain portable,
  SDK-independent logic.
- `tests/` contains dependency-free CTest executables.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and hardware-reporting guidance, and
[SECURITY.md](SECURITY.md) for private vulnerability-reporting instructions. Community participation
is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

This repository's original source code is available under the [MIT License](LICENSE), copyright
2026 Joseph Hawley.

The MIT License applies only to this project's original code. SDRplay software is separate
proprietary software governed by SDRplay's terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the official [SDRplay API page](https://www.sdrplay.com/api/).

SDRplay and RSP1B are names or marks associated with SDRplay Limited. This project is independent and
is not affiliated with, sponsored by, or endorsed by SDRplay Limited. No SDRplay logos are used.
