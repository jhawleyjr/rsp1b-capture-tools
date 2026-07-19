#include "metadata.hpp"

#include "output_file.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace rsp1b {

std::filesystem::path metadataPathFor(const std::filesystem::path& iqPath) {
    std::filesystem::path metadataPath = iqPath;
    std::string extension = iqPath.extension().string();
    for (char& character : extension) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    if (extension == ".txt") {
        metadataPath += ".metadata.txt";
        return metadataPath;
    }
    metadataPath.replace_extension(".txt");
    return metadataPath;
}

std::string renderMetadata(const MetadataRecord& record) {
    const long double expectedSamples = static_cast<long double>(record.options.durationSeconds) *
                                        static_cast<long double>(record.options.sampleRateSps);
    constexpr auto maximumSamples =
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    const std::uint64_t approximateSamples = expectedSamples >= maximumSamples
                                                 ? std::numeric_limits<std::uint64_t>::max()
                                                 : static_cast<std::uint64_t>(expectedSamples);

    std::ostringstream metadata;
    metadata << std::setprecision(15);
    metadata << "receiver = SDRplay RSP1B\n";
    metadata << "serial = " << record.serialNumber << '\n';
    metadata << "center_frequency_hz = " << record.options.centerHz << '\n';
    metadata << "sample_rate_sps = " << record.options.sampleRateSps << '\n';
    metadata << "bandwidth = 5 MHz\n";
    metadata << "if_type = zero IF\n";
    metadata << "bias_t = " << record.options.biasT << '\n';
    metadata << "rf_notch = 0\n";
    metadata << "dab_notch = 0\n";
    metadata << "if_agc = sdrplay_api_AGC_50HZ\n";
    metadata << "duration_seconds_requested = " << record.options.durationSeconds << '\n';
    metadata << "callbacks_received = " << record.statistics.callbacksReceived << '\n';
    metadata << "total_complex_samples_accepted = " << record.statistics.samplesAccepted << '\n';
    metadata << "total_complex_samples_written = " << record.statistics.samplesWritten << '\n';
    metadata << "reset_count = " << record.statistics.resetCount << '\n';
    metadata << "queue_overflow_count = " << record.statistics.queueOverflowCount << '\n';
    metadata << "dropped_block_count = " << record.statistics.droppedBlockCount << '\n';
    metadata << "writer_failure = " << (record.statistics.writerFailure ? 1 : 0) << '\n';
    metadata << "interrupted = " << (record.statistics.interrupted ? 1 : 0) << '\n';
    metadata << "device_removed = " << (record.statistics.deviceRemoved ? 1 : 0) << '\n';
    metadata << "power_overload_event_count = " << record.statistics.overloadEventCount << '\n';
    metadata << "power_overload_ack_failure_count = "
             << record.statistics.overloadAcknowledgementFailures << '\n';
    metadata << "expected_samples_approx = " << approximateSamples << '\n';
    metadata << "output_format = interleaved_int16_iq\n";
    metadata << "byte_order = little_endian\n";
    metadata << "timestamp_local = " << record.timestampLocal << '\n';
    return metadata.str();
}

bool writeMetadataFile(const std::filesystem::path& path, const MetadataRecord& record,
                       bool overwriteAuthorized, std::string& error) {
    bool pathExisted = false;
    if (!checkOutputPath(path, overwriteAuthorized, "metadata output", pathExisted, error)) {
        return false;
    }

    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        error = "Unable to open metadata output: " + path.string();
        return false;
    }

    output << renderMetadata(record);
    output.flush();
    if (!output) {
        error = "Unable to write metadata output: " + path.string();
        return false;
    }
    return true;
}

} // namespace rsp1b
