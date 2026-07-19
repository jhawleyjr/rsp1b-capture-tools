#pragma once

#include <sdrplay_api.h>

#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace rsp1b {

constexpr float kMinimumRsp1bApiVersion = 3.14F;

struct ReceiverConfiguration {
    double centerHz = 1575420000.0;
    double sampleRateSps = 5000000.0;
    int biasT = 0;
};

struct EventState {
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> deviceRemoved{false};
    std::atomic<std::uint64_t> powerOverloadEventCount{0};
    std::atomic<std::uint64_t> powerOverloadAcknowledgementFailures{0};
};

class DeviceSession;

struct DeviceCallbackContext {
    DeviceSession* session = nullptr;
    EventState* events = nullptr;
    void* streamContext = nullptr;
};

class DeviceSession {
  public:
    DeviceSession(std::ostream& output, std::ostream& errors) noexcept;
    ~DeviceSession() noexcept;

    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;
    DeviceSession(DeviceSession&&) = delete;
    DeviceSession& operator=(DeviceSession&&) = delete;

    bool connect();
    bool configure(const ReceiverConfiguration& configuration);
    bool initialise(sdrplay_api_StreamCallback_t streamCallback,
                    DeviceCallbackContext& callbackContext);
    bool stopStreaming();
    bool shutdown();
    [[nodiscard]] bool acknowledgePowerOverload(sdrplay_api_TunerSelectT tuner) const;

    [[nodiscard]] const sdrplay_api_DeviceT& device() const noexcept;
    [[nodiscard]] float runtimeApiVersion() const noexcept;

  private:
    bool callSucceeded(const char* operation, sdrplay_api_ErrT error) const;
    bool disableBiasT();
    void printDevice(const sdrplay_api_DeviceT& device, unsigned int index) const;

    std::ostream& output_;
    std::ostream& errors_;
    bool apiOpen_ = false;
    bool apiLocked_ = false;
    bool deviceSelected_ = false;
    bool deviceInitialised_ = false;
    bool biasTShutdownAttempted_ = false;
    float runtimeApiVersion_ = 0.0F;
    sdrplay_api_DeviceT device_{};
    sdrplay_api_DeviceParamsT* deviceParams_ = nullptr;
};

void eventCallback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner,
                   sdrplay_api_EventParamsT* parameters, void* callbackContext);

} // namespace rsp1b
