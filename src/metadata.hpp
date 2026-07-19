#pragma once

#include "capture_options.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace rsp1b {

struct CaptureStatistics {
    std::uint64_t callbacksReceived = 0;
    std::uint64_t samplesAccepted = 0;
    std::uint64_t samplesWritten = 0;
    std::uint64_t resetCount = 0;
    std::uint64_t queueOverflowCount = 0;
    std::uint64_t droppedBlockCount = 0;
    std::uint64_t overloadEventCount = 0;
    std::uint64_t overloadAcknowledgementFailures = 0;
    bool writerFailure = false;
    bool interrupted = false;
    bool deviceRemoved = false;
};

struct MetadataRecord {
    CaptureOptions options;
    CaptureStatistics statistics;
    std::string serialNumber;
    std::string timestampLocal;
};

std::filesystem::path metadataPathFor(const std::filesystem::path& iqPath);
std::string renderMetadata(const MetadataRecord& record);
bool writeMetadataFile(const std::filesystem::path& path,
                       const MetadataRecord& record,
                       bool overwriteAuthorized,
                       std::string& error);

}  // namespace rsp1b
