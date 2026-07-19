#include "metadata.hpp"

#include "output_file.hpp"
#include "test_support.hpp"
#include "timestamp.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("rsp1b_metadata_test_" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

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
    CHECK_CONTAINS(metadata, "dropped_block_count = 1");
    CHECK_CONTAINS(metadata, "writer_failure = 1");
    CHECK_CONTAINS(metadata, "interrupted = 1");
    CHECK_CONTAINS(metadata, "device_removed = 1");
    CHECK_CONTAINS(metadata, "expected_samples_approx = 12500000");
}

void testPathsAndTimestamp() {
    const std::filesystem::path iqPath = "captures/example.iq";
    const std::filesystem::path rawPath = "captures/example.raw";
    const std::filesystem::path txtPath = "captures/example.txt";
    const std::filesystem::path upperTxtPath = "captures/example.TXT";
    CHECK(rsp1b::metadataPathFor(iqPath) == "captures/example.txt");
    CHECK(rsp1b::metadataPathFor(rawPath) == "captures/example.txt");
    CHECK(rsp1b::metadataPathFor(txtPath) == "captures/example.txt.metadata.txt");
    CHECK(rsp1b::metadataPathFor(upperTxtPath) == "captures/example.TXT.metadata.txt");
    CHECK(rsp1b::metadataPathFor(txtPath) != txtPath);
    CHECK(rsp1b::metadataPathFor(upperTxtPath) != upperTxtPath);

    std::string error;
    CHECK(rsp1b::validateDistinctOutputPaths(
        txtPath, rsp1b::metadataPathFor(txtPath), error));
    CHECK(rsp1b::validateDistinctOutputPaths(
        upperTxtPath, rsp1b::metadataPathFor(upperTxtPath), error));
    CHECK(!rsp1b::validateDistinctOutputPaths(txtPath, txtPath, error));
    CHECK_CONTAINS(error, "--force cannot authorize metadata");
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(!rsp1b::validateDistinctOutputPaths(
        "captures/case-sensitive-name.iq", "captures/CASE-SENSITIVE-NAME.IQ", error));
    CHECK_CONTAINS(error, "resolve to the same path");
#endif
    CHECK(!rsp1b::formatLocalTimestamp(std::time(nullptr), "%Y%m%d_%H%M%S").empty());
}

void testMetadataOverwriteProtection() {
    TemporaryDirectory temporaryDirectory;
    const auto metadataPath = temporaryDirectory.path() / "capture.txt";
    {
        std::ofstream sentinel(metadataPath);
        sentinel << "sentinel metadata";
    }

    rsp1b::MetadataRecord record;
    record.options.durationSeconds = 1.0;
    record.options.outputPath = "capture.iq";
    std::string error;
    CHECK(!rsp1b::writeMetadataFile(metadataPath, record, false, error));
    CHECK_CONTAINS(error, "existing file was not modified");
    CHECK_CONTAINS(error, "--force");
    CHECK(readText(metadataPath) == "sentinel metadata");

    CHECK(rsp1b::writeMetadataFile(metadataPath, record, true, error));
    CHECK_CONTAINS(readText(metadataPath), "receiver = SDRplay RSP1B");
}

void testTxtIqCompanionSafety() {
    TemporaryDirectory temporaryDirectory;
    const auto iqPath = temporaryDirectory.path() / "capture.txt";
    const auto metadataPath = rsp1b::metadataPathFor(iqPath);
    {
        std::ofstream iqSentinel(iqPath);
        iqSentinel << "sentinel IQ data";
        std::ofstream metadataSentinel(metadataPath);
        metadataSentinel << "old metadata";
    }

    rsp1b::MetadataRecord record;
    record.options.durationSeconds = 1.0;
    record.options.outputPath = iqPath;
    std::string error;
    CHECK(rsp1b::writeMetadataFile(metadataPath, record, true, error));
    CHECK(readText(iqPath) == "sentinel IQ data");
    CHECK_CONTAINS(readText(metadataPath), "receiver = SDRplay RSP1B");
}

void testMetadataDirectoryRejection() {
    TemporaryDirectory temporaryDirectory;
    const auto metadataPath = temporaryDirectory.path() / "capture.txt";
    std::filesystem::create_directory(metadataPath);
    const auto childPath = metadataPath / "keep.txt";
    {
        std::ofstream child(childPath);
        child << "directory contents";
    }

    rsp1b::MetadataRecord record;
    for (const bool overwriteAuthorized : {false, true}) {
        std::string error;
        CHECK(!rsp1b::writeMetadataFile(
            metadataPath, record, overwriteAuthorized, error));
        CHECK_CONTAINS(error, "not a regular file");
        CHECK(std::filesystem::is_directory(metadataPath));
        CHECK(readText(childPath) == "directory contents");
    }
}

void testMetadataSymbolicLinkRejection() {
    TemporaryDirectory temporaryDirectory;
    const auto targetPath = temporaryDirectory.path() / "target.txt";
    const auto linkPath = temporaryDirectory.path() / "linked.txt";
    {
        std::ofstream target(targetPath);
        target << "linked metadata sentinel";
    }

    std::error_code symlinkError;
    std::filesystem::create_symlink(targetPath, linkPath, symlinkError);
    if (symlinkError) {
        return;
    }

    rsp1b::MetadataRecord record;
    std::string error;
    CHECK(!rsp1b::writeMetadataFile(linkPath, record, true, error));
    CHECK_CONTAINS(error, "symbolic link");
    CHECK(readText(targetPath) == "linked metadata sentinel");
}

}  // namespace

int main() {
    testMetadataContent();
    testPathsAndTimestamp();
    testMetadataOverwriteProtection();
    testTxtIqCompanionSafety();
    testMetadataDirectoryRejection();
    testMetadataSymbolicLinkRejection();
    return test_support::failures == 0 ? 0 : 1;
}
