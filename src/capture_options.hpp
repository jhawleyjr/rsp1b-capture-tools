#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rsp1b {

constexpr double kDefaultCenterHz = 1575420000.0;
constexpr double kDefaultSampleRateSps = 5000000.0;
constexpr int kDefaultBiasT = 0;

struct CaptureOptions {
    double durationSeconds = 0.0;
    double centerHz = kDefaultCenterHz;
    double sampleRateSps = kDefaultSampleRateSps;
    int biasT = kDefaultBiasT;
    bool force = false;
    std::filesystem::path outputPath;
};

enum class ParseResult {
    success,
    help,
    error,
};

struct ParseOutcome {
    ParseResult result = ParseResult::error;
    CaptureOptions options;
    std::string message;
};

ParseOutcome parseCaptureOptions(const std::vector<std::string>& arguments);
std::string captureUsage(const std::string& programName);

}  // namespace rsp1b
