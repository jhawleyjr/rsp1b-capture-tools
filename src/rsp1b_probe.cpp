#include "rsp1b_device.hpp"
#include "signal_stop.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

namespace {

struct StreamStatistics {
    std::atomic<std::uint64_t> callbackCount{0};
    std::atomic<std::uint64_t> totalSamples{0};
    std::atomic<std::uint64_t> resetCount{0};
    std::atomic<int> xiMin{std::numeric_limits<short>::max()};
    std::atomic<int> xiMax{std::numeric_limits<short>::min()};
    std::atomic<int> xqMin{std::numeric_limits<short>::max()};
    std::atomic<int> xqMax{std::numeric_limits<short>::min()};
};

void updateMinimum(std::atomic<int>& target, int value) {
    int current = target.load(std::memory_order_relaxed);
    while (value < current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void updateMaximum(std::atomic<int>& target, int value) {
    int current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void streamCallback(short* xi, short* xq, sdrplay_api_StreamCbParamsT*, unsigned int sampleCount,
                    unsigned int reset, void* callbackContext) {
    auto* deviceContext = static_cast<rsp1b::DeviceCallbackContext*>(callbackContext);
    if (deviceContext == nullptr || deviceContext->streamContext == nullptr ||
        (sampleCount != 0 && (xi == nullptr || xq == nullptr))) {
        if (deviceContext != nullptr && deviceContext->events != nullptr) {
            deviceContext->events->stopRequested.store(true, std::memory_order_relaxed);
        }
        return;
    }

    auto* statistics = static_cast<StreamStatistics*>(deviceContext->streamContext);
    statistics->callbackCount.fetch_add(1, std::memory_order_relaxed);
    statistics->totalSamples.fetch_add(sampleCount, std::memory_order_relaxed);
    if (reset != 0) {
        statistics->resetCount.fetch_add(1, std::memory_order_relaxed);
    }

    for (unsigned int index = 0; index < sampleCount; ++index) {
        updateMinimum(statistics->xiMin, xi[index]);
        updateMaximum(statistics->xiMax, xi[index]);
        updateMinimum(statistics->xqMin, xq[index]);
        updateMaximum(statistics->xqMax, xq[index]);
    }
}

void printStatistics(const StreamStatistics& statistics) {
    const std::uint64_t totalSamples = statistics.totalSamples.load(std::memory_order_relaxed);
    std::cout << "Stream statistics:\n"
              << "  callbacks=" << statistics.callbackCount.load(std::memory_order_relaxed) << '\n'
              << "  total_complex_samples=" << totalSamples << '\n'
              << "  resets=" << statistics.resetCount.load(std::memory_order_relaxed) << '\n';
    if (totalSamples != 0) {
        std::cout << "  xi_min=" << statistics.xiMin.load(std::memory_order_relaxed) << '\n'
                  << "  xi_max=" << statistics.xiMax.load(std::memory_order_relaxed) << '\n'
                  << "  xq_min=" << statistics.xqMin.load(std::memory_order_relaxed) << '\n'
                  << "  xq_max=" << statistics.xqMax.load(std::memory_order_relaxed) << '\n';
    } else {
        std::cout << "  sample ranges unavailable: no samples received\n";
    }
}

bool startupInterrupted() {
    if (!rsp1b::signalStopRequested()) {
        return false;
    }
    std::cerr << "Probe interrupted during startup; receiver initialization was skipped.\n";
    return true;
}

int runProbe() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::string signalError;
    if (!rsp1b::installStopSignalHandlers(signalError)) {
        std::cerr << signalError << '\n';
        return 1;
    }

    rsp1b::DeviceSession session(std::cout, std::cerr);
    if (!session.connect()) {
        return 1;
    }
    if (startupInterrupted()) {
        return 1;
    }
    if (!session.configure({})) {
        return 1;
    }
    if (startupInterrupted()) {
        return 1;
    }

    StreamStatistics statistics;
    rsp1b::EventState events;
    rsp1b::DeviceCallbackContext callbackContext;
    callbackContext.events = &events;
    callbackContext.streamContext = &statistics;
    if (startupInterrupted()) {
        return 1;
    }
    if (!session.initialise(streamCallback, callbackContext)) {
        return 1;
    }

    std::cout << "Streaming for about 1 second...\n";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline &&
           !events.stopRequested.load(std::memory_order_relaxed)) {
        if (rsp1b::signalStopRequested()) {
            events.stopRequested.store(true, std::memory_order_relaxed);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    bool success = session.stopStreaming();
    if (!session.shutdown()) {
        success = false;
    }
    callbackContext.session = nullptr;
    printStatistics(statistics);
    std::cout << "Event statistics:\n"
              << "  power_overload_events="
              << events.powerOverloadEventCount.load(std::memory_order_relaxed) << '\n'
              << "  power_overload_ack_failures="
              << events.powerOverloadAcknowledgementFailures.load(std::memory_order_relaxed)
              << '\n';

    if (rsp1b::signalStopRequested()) {
        std::cerr << "Probe interrupted; cleanup was attempted on the application thread.\n";
        success = false;
    }
    if (events.deviceRemoved.load(std::memory_order_relaxed) ||
        events.powerOverloadAcknowledgementFailures.load(std::memory_order_relaxed) != 0) {
        success = false;
    }
    return success ? 0 : 1;
}

} // namespace

int main() {
    try {
        return runProbe();
    } catch (const std::exception& exception) {
        std::cerr << "Probe failed: " << exception.what() << '\n';
        return 1;
    }
}
