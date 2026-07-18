#include "metadata.hpp"

#include "test_support.hpp"
#include "timestamp.hpp"

#include <ctime>

namespace {

void testMetadataContent() {
    rsp1b::MetadataRecord record;
    record.options.durationSeconds = 2.5;
    record.options.centerHz = 1575420000.0;
    record.options.sampleRateSps = 5000000.0;
    record.options.biasT = 0;
    record.options.outputPath = "capture.iq";
    record.serialNumber = "REDACTED-TEST";
    record.timestampLocal = "2026-07-18 12:00:00 MST";
    record.statistics.callbacksReceived = 4;
    record.statistics.samplesAccepted = 12;
    record.statistics.samplesWritten = 10;
    record.statistics.resetCount = 2;
    record.statistics.queueOverflowCount = 1;
    record.statistics.droppedBlockCount = 1;
    record.statistics.writerFailure = true;
    record.statistics.interrupted = true;
    record.statistics.deviceRemoved = true;

    const std::string metadata = rsp1b::renderMetadata(record);
    for (const char* key : {"receiver = SDRplay RSP1B",
                            "serial = REDACTED-TEST",
                            "center_frequency_hz = 1575420000",
                            "sample_rate_sps = 5000000",
                            "bandwidth = 5 MHz",
                            "if_type = zero IF",
                            "bias_t = 0",
                            "rf_notch = 0",
                            "dab_notch = 0",
                            "if_agc = sdrplay_api_AGC_50HZ",
                            "duration_seconds_requested = 2.5",
                            "total_complex_samples_written = 10",
                            "output_format = interleaved_int16_iq",
                            "byte_order = little_endian"}) {
        CHECK_CONTAINS(metadata, key);
    }
    CHECK_CONTAINS(metadata, "callbacks_received = 4");
    CHECK_CONTAINS(metadata, "total_complex_samples_accepted = 12");
    CHECK_CONTAINS(metadata, "reset_count = 2");
    CHECK_CONTAINS(metadata, "queue_overflow_count = 1");
    CHECK_CONTAINS(metadata, "writer_failure = 1");
    CHECK_CONTAINS(metadata, "interrupted = 1");
    CHECK_CONTAINS(metadata, "device_removed = 1");
    CHECK_CONTAINS(metadata, "expected_samples_approx = 12500000");
}

void testPathsAndTimestamp() {
    CHECK(rsp1b::metadataPathFor("captures/example.iq") == "captures/example.txt");
    CHECK(rsp1b::metadataPathFor("captures/example.raw") == "captures/example.txt");
    CHECK(!rsp1b::formatLocalTimestamp(std::time(nullptr), "%Y%m%d_%H%M%S").empty());
}

}  // namespace

int main() {
    testMetadataContent();
    testPathsAndTimestamp();
    return test_support::failures == 0 ? 0 : 1;
}
