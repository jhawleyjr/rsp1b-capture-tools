#include "capture_options.hpp"

#include "timestamp.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace rsp1b {
namespace {

bool parsePositiveFiniteDouble(const std::string& text, double& value) {
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed) ||
        parsed <= 0.0) {
        return false;
    }

    value = parsed;
    return true;
}

ParseOutcome errorOutcome(std::string message) {
    ParseOutcome outcome;
    outcome.result = ParseResult::error;
    outcome.message = std::move(message);
    return outcome;
}

}  // namespace

std::string captureUsage(const std::string& programName) {
    return "Usage:\n"
           "  " +
           programName +
           " --duration SECONDS [--out captures/file.iq] [--force]\n"
           "      [--bias-t 0|1] [--center HZ] [--sample-rate SPS]\n";
}

ParseOutcome parseCaptureOptions(const std::vector<std::string>& arguments) {
    const std::string programName = arguments.empty() ? "rsp1b_capture" : arguments.front();
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == "--help" || arguments[i] == "-h") {
            ParseOutcome outcome;
            outcome.result = ParseResult::help;
            outcome.message = captureUsage(programName);
            return outcome;
        }
    }

    CaptureOptions options;
    bool sawDuration = false;
    bool sawOutput = false;

    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        const auto requireValue = [&]() -> const std::string* {
            if (i + 1 >= arguments.size()) {
                return nullptr;
            }
            ++i;
            return &arguments[i];
        };

        if (argument == "--duration") {
            const std::string* value = requireValue();
            if (value == nullptr) {
                return errorOutcome("Missing value for --duration.\n");
            }
            if (!parsePositiveFiniteDouble(*value, options.durationSeconds)) {
                return errorOutcome(
                    "Invalid --duration value; use a finite number greater than zero.\n");
            }
            sawDuration = true;
        } else if (argument == "--out") {
            const std::string* value = requireValue();
            if (value == nullptr) {
                return errorOutcome("Missing value for --out.\n");
            }
            if (value->empty()) {
                return errorOutcome("Invalid --out value; provide a file path.\n");
            }
            options.outputPath = *value;
            sawOutput = true;
        } else if (argument == "--force") {
            options.force = true;
        } else if (argument == "--bias-t") {
            const std::string* value = requireValue();
            if (value == nullptr) {
                return errorOutcome("Missing value for --bias-t.\n");
            }
            if (*value != "0" && *value != "1") {
                return errorOutcome("Invalid --bias-t value; use exactly 0 or 1.\n");
            }
            options.biasT = *value == "1" ? 1 : 0;
        } else if (argument == "--center") {
            const std::string* value = requireValue();
            if (value == nullptr) {
                return errorOutcome("Missing value for --center.\n");
            }
            if (!parsePositiveFiniteDouble(*value, options.centerHz)) {
                return errorOutcome(
                    "Invalid --center value; use a finite number greater than zero.\n");
            }
        } else if (argument == "--sample-rate") {
            const std::string* value = requireValue();
            if (value == nullptr) {
                return errorOutcome("Missing value for --sample-rate.\n");
            }
            if (!parsePositiveFiniteDouble(*value, options.sampleRateSps)) {
                return errorOutcome(
                    "Invalid --sample-rate value; use a finite number greater than zero.\n");
            }
        } else {
            return errorOutcome("Unknown argument: " + argument + "\n");
        }
    }

    if (!sawDuration) {
        return errorOutcome("--duration is required.\n");
    }

    if (!sawOutput) {
        const std::string timestamp = localTimestampForName();
        if (timestamp.empty()) {
            return errorOutcome("Unable to create a timestamp for the default output path.\n");
        }
        options.outputPath =
            std::filesystem::path("captures") / ("rsp1b_capture_" + timestamp + ".iq");
    }

    ParseOutcome outcome;
    outcome.result = ParseResult::success;
    outcome.options = std::move(options);
    return outcome;
}

}  // namespace rsp1b
