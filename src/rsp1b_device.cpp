#include "rsp1b_device.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace rsp1b {
namespace {

const char* errorString(const sdrplay_api_ErrT error) {
    const char* text = sdrplay_api_GetErrorString(error);
    return text != nullptr ? text : "(no error string)";
}

const char* tunerName(sdrplay_api_TunerSelectT tuner) {
    switch (tuner) {
    case sdrplay_api_Tuner_A:
        return "A";
    case sdrplay_api_Tuner_B:
        return "B";
    case sdrplay_api_Tuner_Both:
        return "both";
    default:
        return "unknown";
    }
}

} // namespace

DeviceSession::DeviceSession(std::ostream& output, std::ostream& errors) noexcept
    : output_(output), errors_(errors) {}

DeviceSession::~DeviceSession() noexcept {
    try {
        shutdown();
    } catch (...) {
        // Destructors must not allow diagnostic stream exceptions to escape.
    }
}

bool DeviceSession::connect() {
    if (!callSucceeded("sdrplay_api_Open", sdrplay_api_Open())) {
        return false;
    }
    apiOpen_ = true;

    if (!callSucceeded("sdrplay_api_ApiVersion", sdrplay_api_ApiVersion(&runtimeApiVersion_))) {
        return false;
    }

    output_ << std::fixed << std::setprecision(2)
            << "SDRplay API version: compile-time=" << SDRPLAY_API_VERSION
            << ", runtime=" << runtimeApiVersion_ << '\n';
    if (runtimeApiVersion_ < kMinimumRsp1bApiVersion) {
        errors_ << "RSP1B requires SDRplay API 3.14 or newer.\n";
        return false;
    }
    if (std::fabs(runtimeApiVersion_ - SDRPLAY_API_VERSION) > 0.0005F) {
        errors_ << "SDRplay API version mismatch: this program was compiled against "
                << SDRPLAY_API_VERSION << " but loaded runtime " << runtimeApiVersion_ << ".\n";
        return false;
    }

    if (!callSucceeded("sdrplay_api_LockDeviceApi", sdrplay_api_LockDeviceApi())) {
        return false;
    }
    apiLocked_ = true;

    std::array<sdrplay_api_DeviceT, SDRPLAY_MAX_DEVICES> devices{};
    unsigned int deviceCount = 0;
    if (!callSucceeded("sdrplay_api_GetDevices",
                       sdrplay_api_GetDevices(devices.data(), &deviceCount,
                                              static_cast<unsigned int>(devices.size())))) {
        return false;
    }

    output_ << "Detected devices: " << deviceCount << '\n';
    int selectedIndex = -1;
    for (unsigned int index = 0; index < deviceCount; ++index) {
        printDevice(devices[index], index);
        if (selectedIndex < 0 && devices[index].hwVer == SDRPLAY_RSP1B_ID &&
            devices[index].valid != 0) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex < 0) {
        errors_ << "No available genuine SDRplay RSP1B receiver was found.\n";
        return false;
    }

    device_ = devices[static_cast<std::size_t>(selectedIndex)];
    output_ << "Selected RSP1B:\n";
    printDevice(device_, static_cast<unsigned int>(selectedIndex));
    if (!callSucceeded("sdrplay_api_SelectDevice", sdrplay_api_SelectDevice(&device_))) {
        return false;
    }
    deviceSelected_ = true;

    if (!callSucceeded("sdrplay_api_UnlockDeviceApi", sdrplay_api_UnlockDeviceApi())) {
        return false;
    }
    apiLocked_ = false;

    if (!callSucceeded("sdrplay_api_GetDeviceParams",
                       sdrplay_api_GetDeviceParams(device_.dev, &deviceParams_))) {
        return false;
    }
    if (deviceParams_ == nullptr || deviceParams_->devParams == nullptr ||
        deviceParams_->rxChannelA == nullptr) {
        errors_ << "sdrplay_api_GetDeviceParams returned incomplete RSP1B parameters.\n";
        return false;
    }
    return true;
}

bool DeviceSession::configure(const ReceiverConfiguration& configuration) {
    if (!deviceSelected_ || deviceParams_ == nullptr || deviceParams_->devParams == nullptr ||
        deviceParams_->rxChannelA == nullptr) {
        errors_ << "Cannot configure the receiver before a complete RSP1B session is selected.\n";
        return false;
    }

    deviceParams_->devParams->fsFreq.fsHz = configuration.sampleRateSps;
    deviceParams_->rxChannelA->tunerParams.rfFreq.rfHz = configuration.centerHz;
    deviceParams_->rxChannelA->tunerParams.ifType = sdrplay_api_IF_Zero;
    deviceParams_->rxChannelA->tunerParams.bwType = sdrplay_api_BW_5_000;
    deviceParams_->rxChannelA->rsp1aTunerParams.biasTEnable =
        static_cast<unsigned char>(configuration.biasT);
    deviceParams_->devParams->rsp1aParams.rfNotchEnable = 0;
    deviceParams_->devParams->rsp1aParams.rfDabNotchEnable = 0;
    deviceParams_->rxChannelA->ctrlParams.agc.enable = sdrplay_api_AGC_50HZ;
    biasTShutdownAttempted_ = false;

    output_ << "Configured before Init:\n"
            << "  sample_rate_sps=" << configuration.sampleRateSps << '\n'
            << "  center_frequency_hz=" << configuration.centerHz << '\n'
            << "  if_type=sdrplay_api_IF_Zero\n"
            << "  bandwidth=sdrplay_api_BW_5_000\n"
            << "  bias_t=" << configuration.biasT << '\n'
            << "  rf_notch=0\n"
            << "  dab_notch=0\n"
            << "  if_agc=sdrplay_api_AGC_50HZ\n";
    return true;
}

bool DeviceSession::initialise(sdrplay_api_StreamCallback_t streamCallback,
                               DeviceCallbackContext& callbackContext) {
    if (!deviceSelected_ || streamCallback == nullptr || callbackContext.events == nullptr) {
        errors_ << "Cannot initialise the receiver with an incomplete callback context.\n";
        return false;
    }

    callbackContext.session = this;
    sdrplay_api_CallbackFnsT callbacks{};
    callbacks.StreamACbFn = streamCallback;
    callbacks.StreamBCbFn = nullptr;
    callbacks.EventCbFn = reinterpret_cast<sdrplay_api_EventCallback_t>(eventCallback);
    if (!callSucceeded("sdrplay_api_Init",
                       sdrplay_api_Init(device_.dev, &callbacks, &callbackContext))) {
        callbackContext.session = nullptr;
        return false;
    }
    deviceInitialised_ = true;
    return true;
}

bool DeviceSession::stopStreaming() {
    bool success = true;
    if (deviceSelected_ && !disableBiasT()) {
        success = false;
    }
    if (deviceInitialised_) {
        const sdrplay_api_ErrT error = sdrplay_api_Uninit(device_.dev);
        if (!callSucceeded("sdrplay_api_Uninit", error)) {
            success = false;
        } else {
            deviceInitialised_ = false;
        }
    }
    return success;
}

bool DeviceSession::shutdown() {
    bool success = true;
    if (deviceSelected_ && !disableBiasT()) {
        success = false;
    }
    if (deviceInitialised_) {
        if (!callSucceeded("sdrplay_api_Uninit", sdrplay_api_Uninit(device_.dev))) {
            success = false;
        }
        // Release and API close are still attempted after a failed uninitialisation.
        deviceInitialised_ = false;
    }
    if (deviceSelected_) {
        if (!callSucceeded("sdrplay_api_ReleaseDevice", sdrplay_api_ReleaseDevice(&device_))) {
            success = false;
        }
        deviceSelected_ = false;
        deviceParams_ = nullptr;
    }
    if (apiLocked_) {
        if (!callSucceeded("sdrplay_api_UnlockDeviceApi", sdrplay_api_UnlockDeviceApi())) {
            success = false;
        }
        apiLocked_ = false;
    }
    if (apiOpen_) {
        if (!callSucceeded("sdrplay_api_Close", sdrplay_api_Close())) {
            success = false;
        }
        apiOpen_ = false;
    }
    return success;
}

bool DeviceSession::acknowledgePowerOverload(sdrplay_api_TunerSelectT tuner) const {
    if (!deviceSelected_) {
        errors_ << "Cannot acknowledge a power-overload event without a selected device.\n";
        return false;
    }
    return callSucceeded("sdrplay_api_Update(OverloadMsgAck)",
                         sdrplay_api_Update(device_.dev, tuner,
                                            sdrplay_api_Update_Ctrl_OverloadMsgAck,
                                            sdrplay_api_Update_Ext1_None));
}

const sdrplay_api_DeviceT& DeviceSession::device() const noexcept {
    return device_;
}

float DeviceSession::runtimeApiVersion() const noexcept {
    return runtimeApiVersion_;
}

bool DeviceSession::callSucceeded(const char* operation, sdrplay_api_ErrT error) const {
    if (error == sdrplay_api_Success) {
        output_ << operation << " -> success\n";
        return true;
    }
    errors_ << operation << " failed: " << static_cast<int>(error) << " (" << errorString(error)
            << ")\n";
    return false;
}

bool DeviceSession::disableBiasT() {
    if (biasTShutdownAttempted_) {
        return true;
    }
    if (deviceParams_ == nullptr || deviceParams_->rxChannelA == nullptr) {
        errors_
            << "Shutdown could not request Bias-T off because device parameters are unavailable.\n";
        return false;
    }

    deviceParams_->rxChannelA->rsp1aTunerParams.biasTEnable = 0;
    output_ << "Shutdown: Bias-T requested OFF.\n";
    biasTShutdownAttempted_ = true;
    if (!deviceInitialised_) {
        return true;
    }
    return callSucceeded("sdrplay_api_Update(Rsp1a_BiasTControl)",
                         sdrplay_api_Update(device_.dev, device_.tuner,
                                            sdrplay_api_Update_Rsp1a_BiasTControl,
                                            sdrplay_api_Update_Ext1_None));
}

void DeviceSession::printDevice(const sdrplay_api_DeviceT& device, unsigned int index) const {
    output_ << "  Device[" << index << "] serial=" << device.SerNo
            << " hwVer=" << static_cast<unsigned int>(device.hwVer)
            << " tuner=" << static_cast<int>(device.tuner)
            << " valid=" << static_cast<unsigned int>(device.valid) << '\n';
}

void eventCallback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner,
                   const sdrplay_api_EventParamsT* parameters, void* callbackContext) {
    auto* context = static_cast<DeviceCallbackContext*>(callbackContext);
    if (context == nullptr || context->events == nullptr) {
        return;
    }

    switch (eventId) {
    case sdrplay_api_PowerOverloadChange: {
        context->events->powerOverloadEventCount.fetch_add(1, std::memory_order_relaxed);
        const char* change = "unknown";
        if (parameters != nullptr) {
            change = parameters->powerOverloadParams.powerOverloadChangeType ==
                             sdrplay_api_Overload_Detected
                         ? "detected"
                         : "corrected";
        }
        std::cout << "Event: power overload " << change << " on tuner " << tunerName(tuner)
                  << ".\n";
        if (context->session == nullptr || !context->session->acknowledgePowerOverload(tuner)) {
            context->events->powerOverloadAcknowledgementFailures.fetch_add(
                1, std::memory_order_relaxed);
            context->events->stopRequested.store(true, std::memory_order_relaxed);
        }
        break;
    }
    case sdrplay_api_DeviceRemoved:
        std::cerr << "Event: the selected RSP1B was removed; stopping gracefully.\n";
        context->events->deviceRemoved.store(true, std::memory_order_relaxed);
        context->events->stopRequested.store(true, std::memory_order_relaxed);
        break;
    case sdrplay_api_GainChange:
        if (parameters != nullptr) {
            std::cout << "Event: gain change on tuner " << tunerName(tuner)
                      << ", gRdB=" << parameters->gainParams.gRdB
                      << ", lnaGRdB=" << parameters->gainParams.lnaGRdB
                      << ", systemGain=" << parameters->gainParams.currGain << ".\n";
        } else {
            std::cout << "Event: gain change on tuner " << tunerName(tuner) << ".\n";
        }
        break;
    default:
        std::cout << "Event: id=" << static_cast<int>(eventId) << ", tuner=" << tunerName(tuner)
                  << ".\n";
        break;
    }
}

} // namespace rsp1b
