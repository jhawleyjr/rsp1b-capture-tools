#include <sdrplay_api.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

namespace {

struct StreamStats {
    std::atomic<std::uint64_t> callbackCount{0};
    std::atomic<std::uint64_t> totalSamples{0};
    std::atomic<std::uint64_t> resetCount{0};
    std::atomic<int> xiMin{std::numeric_limits<short>::max()};
    std::atomic<int> xiMax{std::numeric_limits<short>::min()};
    std::atomic<int> xqMin{std::numeric_limits<short>::max()};
    std::atomic<int> xqMax{std::numeric_limits<short>::min()};
};

void updateMin(std::atomic<int>& target, int value) {
    int current = target.load(std::memory_order_relaxed);
    while (value < current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void updateMax(std::atomic<int>& target, int value) {
    int current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

const char* errorString(sdrplay_api_ErrT err) {
    const char* text = sdrplay_api_GetErrorString(err);
    return text != nullptr ? text : "(no error string)";
}

bool checkCall(const char* name, sdrplay_api_ErrT err) {
    std::cout << name << " -> " << static_cast<int>(err) << " ("
              << errorString(err) << ")\n";
    return err == sdrplay_api_Success;
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
    auto* stats = static_cast<StreamStats*>(cbContext);
    if (stats == nullptr) {
        return;
    }

    stats->callbackCount.fetch_add(1, std::memory_order_relaxed);
    stats->totalSamples.fetch_add(numSamples, std::memory_order_relaxed);
    if (reset != 0) {
        stats->resetCount.fetch_add(1, std::memory_order_relaxed);
    }

    for (unsigned int i = 0; i < numSamples; ++i) {
        updateMin(stats->xiMin, xi[i]);
        updateMax(stats->xiMax, xi[i]);
        updateMin(stats->xqMin, xq[i]);
        updateMax(stats->xqMax, xq[i]);
    }
}

void eventCallback(sdrplay_api_EventT eventId,
                   sdrplay_api_TunerSelectT tuner,
                   sdrplay_api_EventParamsT*,
                   void*) {
    std::cout << "Event callback: eventId=" << static_cast<int>(eventId)
              << " tuner=" << static_cast<int>(tuner) << '\n';
}

void printStats(const StreamStats& stats) {
    const auto totalSamples = stats.totalSamples.load(std::memory_order_relaxed);
    std::cout << "Stream stats:\n";
    std::cout << "  callbacks=" << stats.callbackCount.load(std::memory_order_relaxed) << '\n';
    std::cout << "  totalSamples=" << totalSamples << '\n';
    if (totalSamples > 0) {
        std::cout << "  xiMin=" << stats.xiMin.load(std::memory_order_relaxed) << '\n';
        std::cout << "  xiMax=" << stats.xiMax.load(std::memory_order_relaxed) << '\n';
        std::cout << "  xqMin=" << stats.xqMin.load(std::memory_order_relaxed) << '\n';
        std::cout << "  xqMax=" << stats.xqMax.load(std::memory_order_relaxed) << '\n';
    } else {
        std::cout << "  xiMin/xiMax/xqMin/xqMax unavailable: no samples received\n";
    }
    std::cout << "  resets=" << stats.resetCount.load(std::memory_order_relaxed) << '\n';
}

}  // namespace

int main() {
    std::cout << std::unitbuf;

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

    deviceParams->devParams->fsFreq.fsHz = 5000000.0;
    deviceParams->rxChannelA->tunerParams.rfFreq.rfHz = 1575420000.0;
    deviceParams->rxChannelA->tunerParams.ifType = sdrplay_api_IF_Zero;
    deviceParams->rxChannelA->tunerParams.bwType = sdrplay_api_BW_5_000;
    deviceParams->rxChannelA->rsp1aTunerParams.biasTEnable = 0;
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

    StreamStats stats;
    sdrplay_api_CallbackFnsT cbFns{};
    cbFns.StreamACbFn = streamACallback;
    cbFns.StreamBCbFn = nullptr;
    cbFns.EventCbFn = eventCallback;

    if (!checkCall("sdrplay_api_Init", sdrplay_api_Init(chosenDevice.dev, &cbFns, &stats))) {
        cleanup();
        return exitCode;
    }
    deviceInitialised = true;

    std::cout << "Streaming for about 1 second...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!checkCall("sdrplay_api_Uninit", sdrplay_api_Uninit(chosenDevice.dev))) {
        cleanup();
        return exitCode;
    }
    deviceInitialised = false;

    printStats(stats);

    exitCode = 0;
    cleanup();
    return exitCode;
}
