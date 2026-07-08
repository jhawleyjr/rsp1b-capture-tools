#include <sdrplay_api.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kDefaultCenterHz = 1575420000.0;
constexpr double kDefaultSampleRateSps = 5000000.0;
constexpr int kDefaultBiasT = 0;

struct CaptureConfig {
    double durationSeconds = 0.0;
    double centerHz = kDefaultCenterHz;
    double sampleRateSps = kDefaultSampleRateSps;
    int biasT = kDefaultBiasT;
    std::filesystem::path outPath;
};

struct CaptureContext {
    std::ofstream iqFile;
    std::mutex writeMutex;
    std::atomic<std::uint64_t> callbackCount{0};
    std::atomic<std::uint64_t> samplesWritten{0};
    std::atomic<std::uint64_t> resetCount{0};
    std::atomic<bool> writeFailed{false};
};

const char* errorString(sdrplay_api_ErrT err) {
    const char* text = sdrplay_api_GetErrorString(err);
    return text != nullptr ? text : "(no error string)";
}

bool checkCall(const char* name, sdrplay_api_ErrT err) {
    std::cout << name << " -> " << static_cast<int>(err) << " ("
              << errorString(err) << ")\n";
    return err == sdrplay_api_Success;
}

void printUsage(const char* argv0) {
    std::cout
        << "Usage:\n"
        << "  " << argv0 << " --duration SECONDS [--out captures/file.iq]\n"
        << "              [--bias-t 0|1] [--center HZ] [--sample-rate SPS]\n";
}

bool parseDouble(const std::string& text, double* value) {
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

bool parseInt(const std::string& text, int* value) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

std::string localTimestampForName() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local);
    return buffer;
}

std::string localTimestampMetadata() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    char buffer[64]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &local);
    return buffer;
}

std::filesystem::path defaultOutputPath() {
    return std::filesystem::path("captures") /
           ("rsp1b_capture_" + localTimestampForName() + ".iq");
}

std::filesystem::path metadataPathFor(const std::filesystem::path& iqPath) {
    std::filesystem::path metaPath = iqPath;
    metaPath.replace_extension(".txt");
    return metaPath;
}

bool parseArgs(int argc, char** argv, CaptureConfig* config) {
    bool sawDuration = false;
    config->outPath = defaultOutputPath();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cout << "Missing value for " << name << ".\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        if (arg == "--duration") {
            const char* value = needValue("--duration");
            if (value == nullptr || !parseDouble(value, &config->durationSeconds)) {
                std::cout << "Invalid --duration value.\n";
                return false;
            }
            sawDuration = true;
        } else if (arg == "--out") {
            const char* value = needValue("--out");
            if (value == nullptr) {
                return false;
            }
            config->outPath = value;
        } else if (arg == "--bias-t") {
            const char* value = needValue("--bias-t");
            if (value == nullptr || !parseInt(value, &config->biasT) ||
                (config->biasT != 0 && config->biasT != 1)) {
                std::cout << "Invalid --bias-t value; use 0 or 1.\n";
                return false;
            }
        } else if (arg == "--center") {
            const char* value = needValue("--center");
            if (value == nullptr || !parseDouble(value, &config->centerHz)) {
                std::cout << "Invalid --center value.\n";
                return false;
            }
        } else if (arg == "--sample-rate") {
            const char* value = needValue("--sample-rate");
            if (value == nullptr || !parseDouble(value, &config->sampleRateSps)) {
                std::cout << "Invalid --sample-rate value.\n";
                return false;
            }
        } else {
            std::cout << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return false;
        }
    }

    if (!sawDuration || config->durationSeconds <= 0.0) {
        std::cout << "--duration is required and must be greater than zero.\n";
        printUsage(argv[0]);
        return false;
    }

    if (config->sampleRateSps <= 0.0) {
        std::cout << "--sample-rate must be greater than zero.\n";
        return false;
    }

    return true;
}

void printDevice(const sdrplay_api_DeviceT& device, unsigned int index) {
    std::cout << "Device[" << index << "]"
              << " SerNo=" << device.SerNo
              << " hwVer=" << static_cast<unsigned int>(device.hwVer)
              << " tuner=" << static_cast<int>(device.tuner)
              << " valid=" << static_cast<unsigned int>(device.valid)
              << " dev=" << device.dev << '\n';
}

void streamACallback(short* xi,
                     short* xq,
                     sdrplay_api_StreamCbParamsT*,
                     unsigned int numSamples,
                     unsigned int reset,
                     void* cbContext) {
    auto* capture = static_cast<CaptureContext*>(cbContext);
    if (capture == nullptr || capture->writeFailed.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<short> interleaved;
    interleaved.resize(static_cast<std::size_t>(numSamples) * 2U);
    for (unsigned int i = 0; i < numSamples; ++i) {
        const std::size_t outIndex = static_cast<std::size_t>(i) * 2U;
        interleaved[outIndex] = xi[i];
        interleaved[outIndex + 1U] = xq[i];
    }

    {
        std::lock_guard<std::mutex> lock(capture->writeMutex);
        capture->iqFile.write(reinterpret_cast<const char*>(interleaved.data()),
                              static_cast<std::streamsize>(interleaved.size() * sizeof(short)));
        if (!capture->iqFile) {
            capture->writeFailed.store(true, std::memory_order_relaxed);
            return;
        }
    }

    capture->callbackCount.fetch_add(1, std::memory_order_relaxed);
    capture->samplesWritten.fetch_add(numSamples, std::memory_order_relaxed);
    if (reset != 0) {
        capture->resetCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void eventCallback(sdrplay_api_EventT eventId,
                   sdrplay_api_TunerSelectT tuner,
                   sdrplay_api_EventParamsT*,
                   void*) {
    std::cout << "Event callback: eventId=" << static_cast<int>(eventId)
              << " tuner=" << static_cast<int>(tuner) << '\n';
}

bool writeMetadata(const std::filesystem::path& path,
                   const CaptureConfig& config,
                   const sdrplay_api_DeviceT& device,
                   std::uint64_t samplesWritten) {
    std::ofstream meta(path);
    if (!meta) {
        std::cout << "Failed to open metadata output: " << path << '\n';
        return false;
    }

    meta << "receiver = SDRplay RSP1B\n";
    meta << "serial = " << device.SerNo << '\n';
    meta << "center_frequency_hz = " << static_cast<std::uint64_t>(config.centerHz) << '\n';
    meta << "sample_rate_sps = " << static_cast<std::uint64_t>(config.sampleRateSps) << '\n';
    meta << "bandwidth = 5 MHz\n";
    meta << "if_type = zero IF\n";
    meta << "bias_t = " << config.biasT << '\n';
    meta << "rf_notch = 0\n";
    meta << "dab_notch = 0\n";
    meta << "if_agc = sdrplay_api_AGC_50HZ\n";
    meta << "duration_seconds_requested = " << config.durationSeconds << '\n';
    meta << "total_complex_samples_written = " << samplesWritten << '\n';
    meta << "output_format = interleaved_int16_iq\n";
    meta << "byte_order = little_endian\n";
    meta << "timestamp_local = " << localTimestampMetadata() << '\n';
    return static_cast<bool>(meta);
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;

    CaptureConfig config;
    if (!parseArgs(argc, argv, &config)) {
        return 1;
    }

    if (config.biasT == 1) {
        std::cout << "WARNING: Bias-T was explicitly requested with --bias-t 1. "
                  << "This will enable antenna DC power.\n";
    } else {
        std::cout << "Bias-T is OFF. Use --bias-t 1 only when DC power is required.\n";
    }

    std::filesystem::create_directories(config.outPath.parent_path().empty()
                                            ? std::filesystem::path(".")
                                            : config.outPath.parent_path());
    const std::filesystem::path metaPath = metadataPathFor(config.outPath);

    CaptureContext capture;
    capture.iqFile.open(config.outPath, std::ios::binary | std::ios::trunc);
    if (!capture.iqFile) {
        std::cout << "Failed to open IQ output: " << config.outPath << '\n';
        return 1;
    }

    bool apiOpen = false;
    bool apiLocked = false;
    bool deviceSelected = false;
    bool deviceInitialised = false;
    sdrplay_api_DeviceT chosenDevice{};
    int exitCode = 1;

    auto cleanup = [&]() {
        if (deviceInitialised) {
            checkCall("sdrplay_api_Uninit", sdrplay_api_Uninit(chosenDevice.dev));
            deviceInitialised = false;
        }
        if (deviceSelected) {
            checkCall("sdrplay_api_ReleaseDevice", sdrplay_api_ReleaseDevice(&chosenDevice));
            deviceSelected = false;
        }
        if (apiLocked) {
            checkCall("sdrplay_api_UnlockDeviceApi", sdrplay_api_UnlockDeviceApi());
            apiLocked = false;
        }
        if (apiOpen) {
            checkCall("sdrplay_api_Close", sdrplay_api_Close());
            apiOpen = false;
        }
    };

    if (!checkCall("sdrplay_api_Open", sdrplay_api_Open())) {
        return exitCode;
    }
    apiOpen = true;

    float apiVersion = 0.0F;
    if (!checkCall("sdrplay_api_ApiVersion", sdrplay_api_ApiVersion(&apiVersion))) {
        cleanup();
        return exitCode;
    }
    std::cout << "SDRplay API version: " << apiVersion << '\n';

    if (!checkCall("sdrplay_api_LockDeviceApi", sdrplay_api_LockDeviceApi())) {
        cleanup();
        return exitCode;
    }
    apiLocked = true;

    std::array<sdrplay_api_DeviceT, SDRPLAY_MAX_DEVICES> devices{};
    unsigned int numDevices = 0;
    if (!checkCall("sdrplay_api_GetDevices",
                   sdrplay_api_GetDevices(devices.data(), &numDevices, devices.size()))) {
        cleanup();
        return exitCode;
    }

    std::cout << "Detected devices: " << numDevices << '\n';
    int selectedIndex = -1;
    for (unsigned int i = 0; i < numDevices; ++i) {
        printDevice(devices[i], i);
        if (selectedIndex < 0 && devices[i].hwVer == SDRPLAY_RSP1B_ID && devices[i].valid != 0) {
            selectedIndex = static_cast<int>(i);
        }
    }

    if (selectedIndex < 0) {
        std::cout << "No valid RSP1B found. Expected hwVer SDRPLAY_RSP1B_ID="
                  << SDRPLAY_RSP1B_ID << ".\n";
        cleanup();
        return exitCode;
    }

    chosenDevice = devices[static_cast<std::size_t>(selectedIndex)];
    std::cout << "Selected RSP1B:\n";
    printDevice(chosenDevice, static_cast<unsigned int>(selectedIndex));

    if (!checkCall("sdrplay_api_SelectDevice", sdrplay_api_SelectDevice(&chosenDevice))) {
        cleanup();
        return exitCode;
    }
    deviceSelected = true;

    if (!checkCall("sdrplay_api_UnlockDeviceApi", sdrplay_api_UnlockDeviceApi())) {
        cleanup();
        return exitCode;
    }
    apiLocked = false;

    sdrplay_api_DeviceParamsT* deviceParams = nullptr;
    if (!checkCall("sdrplay_api_GetDeviceParams",
                   sdrplay_api_GetDeviceParams(chosenDevice.dev, &deviceParams))) {
        cleanup();
        return exitCode;
    }

    if (deviceParams == nullptr || deviceParams->devParams == nullptr ||
        deviceParams->rxChannelA == nullptr) {
        std::cout << "sdrplay_api_GetDeviceParams returned incomplete parameter pointers.\n";
        cleanup();
        return exitCode;
    }

    deviceParams->devParams->fsFreq.fsHz = config.sampleRateSps;
    deviceParams->rxChannelA->tunerParams.rfFreq.rfHz = config.centerHz;
    deviceParams->rxChannelA->tunerParams.ifType = sdrplay_api_IF_Zero;
    deviceParams->rxChannelA->tunerParams.bwType = sdrplay_api_BW_5_000;
    deviceParams->rxChannelA->rsp1aTunerParams.biasTEnable =
        static_cast<unsigned char>(config.biasT);
    deviceParams->devParams->rsp1aParams.rfNotchEnable = 0;
    deviceParams->devParams->rsp1aParams.rfDabNotchEnable = 0;
    deviceParams->rxChannelA->ctrlParams.agc.enable = sdrplay_api_AGC_50HZ;

    std::cout << "Configured before Init:\n";
    std::cout << "  fsHz=" << deviceParams->devParams->fsFreq.fsHz << '\n';
    std::cout << "  rfHz=" << deviceParams->rxChannelA->tunerParams.rfFreq.rfHz << '\n';
    std::cout << "  ifType=sdrplay_api_IF_Zero\n";
    std::cout << "  bwType=sdrplay_api_BW_5_000\n";
    std::cout << "  biasTEnable="
              << static_cast<unsigned int>(deviceParams->rxChannelA->rsp1aTunerParams.biasTEnable)
              << '\n';
    std::cout << "  rfNotchEnable="
              << static_cast<unsigned int>(deviceParams->devParams->rsp1aParams.rfNotchEnable)
              << '\n';
    std::cout << "  rfDabNotchEnable="
              << static_cast<unsigned int>(deviceParams->devParams->rsp1aParams.rfDabNotchEnable)
              << '\n';
    std::cout << "  IF AGC enabled: sdrplay_api_AGC_50HZ\n";

    sdrplay_api_CallbackFnsT cbFns{};
    cbFns.StreamACbFn = streamACallback;
    cbFns.StreamBCbFn = nullptr;
    cbFns.EventCbFn = eventCallback;

    if (!checkCall("sdrplay_api_Init", sdrplay_api_Init(chosenDevice.dev, &cbFns, &capture))) {
        cleanup();
        return exitCode;
    }
    deviceInitialised = true;

    std::cout << "Writing IQ to " << config.outPath << '\n';
    std::cout << "Streaming for " << config.durationSeconds << " seconds...\n";
    std::this_thread::sleep_for(std::chrono::duration<double>(config.durationSeconds));

    if (!checkCall("sdrplay_api_Uninit", sdrplay_api_Uninit(chosenDevice.dev))) {
        cleanup();
        return exitCode;
    }
    deviceInitialised = false;

    {
        std::lock_guard<std::mutex> lock(capture.writeMutex);
        capture.iqFile.flush();
        capture.iqFile.close();
    }

    const std::uint64_t samplesWritten =
        capture.samplesWritten.load(std::memory_order_relaxed);
    std::cout << "Capture stats:\n";
    std::cout << "  callbacks=" << capture.callbackCount.load(std::memory_order_relaxed) << '\n';
    std::cout << "  total_complex_samples_written=" << samplesWritten << '\n';
    std::cout << "  resets=" << capture.resetCount.load(std::memory_order_relaxed) << '\n';

    const double expectedSamples = config.durationSeconds * config.sampleRateSps;
    std::cout << "  expected_samples_approx=" << static_cast<std::uint64_t>(expectedSamples)
              << '\n';

    if (capture.writeFailed.load(std::memory_order_relaxed)) {
        std::cout << "IQ write failed during streaming.\n";
        cleanup();
        return exitCode;
    }

    if (!writeMetadata(metaPath, config, chosenDevice, samplesWritten)) {
        cleanup();
        return exitCode;
    }
    std::cout << "Wrote metadata to " << metaPath << '\n';

    exitCode = 0;
    cleanup();
    return exitCode;
}
