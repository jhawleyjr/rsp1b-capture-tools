# SDRplay RSP1B Tools

Minimal terminal-only SDRplay RSP1B API tools for macOS.

This is capture-only code. It does not implement GPS receiver logic, live receiver integration, or long IQ capture.

- `rsp1b_probe`: opens the API, configures the RSP1B, streams for about one second, prints callback/sample statistics, and exits.
- `rsp1b_capture`: opens the API, configures the RSP1B, writes a short raw interleaved int16 IQ capture plus text metadata, and exits.

## Build and Run

`cmake` was not present at `/opt/homebrew/bin/cmake` or `/usr/local/bin/cmake` on this Mac when this probe was created. If CMake is installed later and visible on `PATH`, configure with:

```sh
cmake -S . -B build
```

Build with:

```sh
cmake --build build
```

Run the probe:

```sh
./build/rsp1b_probe
```

Run a 2-second capture:

```sh
./build/rsp1b_capture --duration 2 --out captures/test_2s.iq
```

## Direct clang++ Fallback

Build the probe without CMake:

```sh
mkdir -p build
clang++ -std=c++17 -Wall -Wextra -pedantic -I/usr/local/include src/rsp1b_probe.cpp /usr/local/lib/libsdrplay_api.dylib -Wl,-rpath,/usr/local/lib -o build/rsp1b_probe
```

Build the capture utility without CMake:

```sh
mkdir -p build
clang++ -std=c++17 -Wall -Wextra -pedantic -I/usr/local/include src/rsp1b_capture.cpp /usr/local/lib/libsdrplay_api.dylib -Wl,-rpath,/usr/local/lib -o build/rsp1b_capture
```

Run the probe:

```sh
./build/rsp1b_probe
```

Run a 2-second capture:

```sh
./build/rsp1b_capture --duration 2 --out captures/test_2s.iq
```

## SDRplay API Paths

The CMake target includes headers from:

```text
/usr/local/include
```

It links directly to:

```text
/usr/local/lib/libsdrplay_api.dylib
```

The probe expects the SDRplay API service to be installed and running, for example:

```text
/Library/SDRplayAPI/3.15.1/bin/sdrplay_apiService
```

## Capture CLI

Required:

```sh
./build/rsp1b_capture --duration 2
```

Optional:

```sh
./build/rsp1b_capture --duration 2 --out captures/test_2s.iq
./build/rsp1b_capture --duration 2 --center 1575420000
./build/rsp1b_capture --duration 2 --sample-rate 5000000
./build/rsp1b_capture --duration 2 --bias-t 1
```

The default output folder is `captures/`. If `--out` is omitted, the capture utility writes a timestamped `.iq` file there. For `captures/test_2s.iq`, metadata is written to `captures/test_2s.txt`.

Bias-T defaults to off. The capture utility enables Bias-T only when `--bias-t 1` is explicitly provided, and it prints a warning before doing so.
During shutdown, both tools explicitly request Bias-T off before uninitializing and releasing the device.

Raw IQ output format:

```text
I0, Q0, I1, Q1, ...
```

Each I and Q value is a little-endian signed 16-bit integer.

## SDR Settings

- Sample rate: `5000000.0` Hz
- RF frequency: `1575420000.0` Hz
- IF type: `sdrplay_api_IF_Zero`
- Bandwidth: `sdrplay_api_BW_5_000`
- Bias-T: disabled
- RF notch: disabled
- RF DAB notch: disabled
- IF AGC: `sdrplay_api_AGC_50HZ`

In the installed 3.15.1 headers, Bias-T is under `rxChannelA->rsp1aTunerParams`, while the RF notch fields are under `devParams->rsp1aParams`.
