#include "capture_options.hpp"
#include "iq_writer.hpp"
#include "metadata.hpp"
#include "rsp1b_device.hpp"
#include "signal_stop.hpp"
#include "timestamp.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static_assert(sizeof(short) == 2, "The SDRplay stream callback requires 16-bit short samples");

namespace {

struct CaptureStreamState {
    rsp1b::IqWriter* writer = nullptr;
    std::atomic<std::uint64_t> callbackCount{0};
    std::atomic<std::uint64_t> resetCount{0};
    std::atomic<bool> callbackFailure{false};
};

std::uint64_t approximateExpectedSamples(const rsp1b::CaptureOptions& options) {
    const long double expected = static_cast<long double>(options.durationSeconds) *
                                 static_cast<long double>(options.sampleRateSps);
    const long double maximum =
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    return expected >= maximum ? std::numeric_limits<std::uint64_t>::max()
                               : static_cast<std::uint64_t>(expected);
}

void streamCallback(short* xi,
                    short* xq,
                    sdrplay_api_StreamCbParamsT*,
                    unsigned int sampleCount,
                    unsigned int reset,
                    void* callbackContext) {
    auto* deviceContext = static_cast<rsp1b::DeviceCallbackContext*>(callbackContext);
    if (deviceContext == nullptr || deviceContext->events == nullptr ||
        deviceContext->streamContext == nullptr) {
        return;
    }

    auto* capture = static_cast<CaptureStreamState*>(deviceContext->streamContext);
    capture->callbackCount.fetch_add(1, std::memory_order_relaxed);
    if (reset != 0) {
        capture->resetCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (capture->writer == nullptr || (sampleCount != 0 && (xi == nullptr || xq == nullptr))) {
        capture->callbackFailure.store(true, std::memory_order_relaxed);
        deviceContext->events->stopRequested.store(true, std::memory_order_relaxed);
        return;
    }

    try {
        rsp1b::IqBlock block(static_cast<std::size_t>(sampleCount) * 2U);
        for (unsigned int index = 0; index < sampleCount; ++index) {
            const std::size_t outputIndex = static_cast<std::size_t>(index) * 2U;
            block[outputIndex] = static_cast<std::int16_t>(xi[index]);
            block[outputIndex + 1U] = static_cast<std::int16_t>(xq[index]);
        }

        const rsp1b::EnqueueResult result = capture->writer->enqueue(std::move(block));
        if (result != rsp1b::EnqueueResult::accepted) {
            deviceContext->events->stopRequested.store(true, std::memory_order_relaxed);
        }
    } catch (...) {
        capture->callbackFailure.store(true, std::memory_order_relaxed);
        deviceContext->events->stopRequested.store(true, std::memory_order_relaxed);
    }
}

bool createOutputDirectory(const std::filesystem::path& outputPath, std::string& error) {
    const std::filesystem::path directory =
        outputPath.parent_path().empty() ? std::filesystem::path(".") : outputPath.parent_path();
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        error = "Unable to create output directory '" + directory.string() + "': " +
                filesystemError.message();
        return false;
    }
    return true;
}

void removeEmptyCapture(const std::filesystem::path& outputPath) {
    std::error_code filesystemError;
    if (std::filesystem::file_size(outputPath, filesystemError) == 0 && !filesystemError) {
        std::filesystem::remove(outputPath, filesystemError);
    }
}

int runCapture(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const rsp1b::ParseOutcome parsed = rsp1b::parseCaptureOptions(arguments);
    if (parsed.result == rsp1b::ParseResult::help) {
        std::cout << parsed.message;
        return 0;
    }
    if (parsed.result == rsp1b::ParseResult::error) {
        std::cerr << parsed.message << rsp1b::captureUsage(arguments.front());
        return 2;
    }
    const rsp1b::CaptureOptions& options = parsed.options;

    std::string signalError;
    if (!rsp1b::installStopSignalHandlers(signalError)) {
        std::cerr << signalError << '\n';
        return 1;
    }

    if (options.biasT == 1) {
        std::cerr << "WARNING: --bias-t 1 enables antenna DC power. Confirm the connected "
                     "hardware is Bias-T safe before continuing.\n";
    } else {
        std::cout << "Bias-T is OFF. Use --bias-t 1 only when antenna DC power is required.\n";
    }

    std::string filesystemError;
    if (!createOutputDirectory(options.outputPath, filesystemError)) {
        std::cerr << filesystemError << '\n';
        return 1;
    }

    std::unique_ptr<rsp1b::IqWriter> writer;
    rsp1b::EventState events;
    CaptureStreamState capture;
    rsp1b::DeviceCallbackContext callbackContext;
    callbackContext.events = &events;
    callbackContext.streamContext = &capture;
    rsp1b::DeviceSession session(std::cout, std::cerr);
    if (!session.connect() ||
        !session.configure({options.centerHz, options.sampleRateSps, options.biasT})) {
        return 1;
    }

    std::string writerError;
    writer = rsp1b::IqWriter::openFile(
        options.outputPath, rsp1b::kDefaultMaxQueuedBlocks, &events.stopRequested, writerError);
    if (writer == nullptr) {
        std::cerr << writerError << '\n';
        return 1;
    }
    capture.writer = writer.get();
    if (!session.initialise(streamCallback, callbackContext)) {
        writer->finish();
        removeEmptyCapture(options.outputPath);
        return 1;
    }

    std::cout << "Writing IQ to " << options.outputPath << '\n'
              << "Streaming for " << options.durationSeconds << " seconds...\n";
    const auto start = std::chrono::steady_clock::now();
    const auto requestedDuration = std::chrono::duration<double>(options.durationSeconds);
    bool interrupted = false;
    while (std::chrono::steady_clock::now() - start < requestedDuration &&
           !events.stopRequested.load(std::memory_order_relaxed)) {
        if (rsp1b::signalStopRequested()) {
            interrupted = true;
            events.stopRequested.store(true, std::memory_order_relaxed);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (rsp1b::signalStopRequested()) {
        interrupted = true;
    }

    const std::string serialNumber = session.device().SerNo;
    bool success = session.stopStreaming();
    if (!session.shutdown()) {
        success = false;
    }
    callbackContext.session = nullptr;
    capture.writer = nullptr;
    if (!writer->finish()) {
        success = false;
    }

    const rsp1b::WriterStatistics writerStatistics = writer->statistics();
    rsp1b::CaptureStatistics statistics;
    statistics.callbacksReceived = capture.callbackCount.load(std::memory_order_relaxed);
    statistics.samplesAccepted = writerStatistics.samplesAccepted;
    statistics.samplesWritten = writerStatistics.samplesWritten;
    statistics.resetCount = capture.resetCount.load(std::memory_order_relaxed);
    statistics.queueOverflowCount = writerStatistics.queueOverflowCount;
    statistics.droppedBlockCount = writerStatistics.droppedBlockCount;
    statistics.writerFailure = writerStatistics.writerFailure;
    statistics.interrupted = interrupted;
    statistics.deviceRemoved = events.deviceRemoved.load(std::memory_order_relaxed);
    statistics.overloadEventCount =
        events.powerOverloadEventCount.load(std::memory_order_relaxed);
    statistics.overloadAcknowledgementFailures =
        events.powerOverloadAcknowledgementFailures.load(std::memory_order_relaxed);

    std::cout << "Capture statistics:\n"
              << "  callbacks_received=" << statistics.callbacksReceived << '\n'
              << "  samples_accepted=" << statistics.samplesAccepted << '\n'
              << "  samples_written=" << statistics.samplesWritten << '\n'
              << "  resets=" << statistics.resetCount << '\n'
              << "  queue_overflows=" << statistics.queueOverflowCount << '\n'
              << "  dropped_blocks=" << statistics.droppedBlockCount << '\n'
              << "  writer_failure=" << (statistics.writerFailure ? 1 : 0) << '\n'
              << "  interrupted=" << (statistics.interrupted ? 1 : 0) << '\n'
              << "  expected_samples_approx=" << approximateExpectedSamples(options) << '\n';

    if (capture.callbackFailure.load(std::memory_order_relaxed)) {
        std::cerr << "The stream callback encountered invalid input or could not allocate an IQ block.\n";
        success = false;
    }
    if (statistics.queueOverflowCount != 0) {
        std::cerr << "Capture stopped because the bounded IQ writer queue overflowed.\n";
        success = false;
    }
    if (statistics.writerFailure) {
        std::cerr << writer->failureMessage() << '\n';
        success = false;
    }
    if (statistics.interrupted) {
        std::cerr << "Capture interrupted; partial data was drained and normal-thread cleanup was "
                     "attempted.\n";
        success = false;
    }
    if (statistics.deviceRemoved) {
        success = false;
    }
    if (statistics.overloadAcknowledgementFailures != 0) {
        std::cerr << "One or more power-overload acknowledgements failed.\n";
        success = false;
    }

    std::string timestamp = rsp1b::localTimestampMetadata();
    if (timestamp.empty()) {
        std::cerr << "Unable to format the metadata timestamp.\n";
        timestamp = "unavailable";
        success = false;
    }

    rsp1b::MetadataRecord metadata;
    metadata.options = options;
    metadata.statistics = statistics;
    metadata.serialNumber = serialNumber;
    metadata.timestampLocal = timestamp;
    const std::filesystem::path metadataPath = rsp1b::metadataPathFor(options.outputPath);
    std::string metadataError;
    if (!rsp1b::writeMetadataFile(metadataPath, metadata, metadataError)) {
        std::cerr << metadataError << '\n';
        success = false;
    } else {
        std::cout << "Wrote metadata to " << metadataPath << '\n';
    }

    return success ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return runCapture(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "Capture failed: " << exception.what() << '\n';
        return 1;
    }
}
