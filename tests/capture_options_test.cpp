#include "capture_options.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace {

rsp1b::ParseOutcome parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> values;
    for (const char* argument : arguments) {
        values.emplace_back(argument);
    }
    return rsp1b::parseCaptureOptions(values);
}

void testValidOptions() {
    const auto outcome = parse({"capture",
                                "--duration",
                                "2.5",
                                "--out",
                                "test.iq",
                                "--force",
                                "--bias-t",
                                "1",
                                "--center",
                                "100.25",
                                "--sample-rate",
                                "2000000.5"});
    CHECK(outcome.result == rsp1b::ParseResult::success);
    CHECK(outcome.options.durationSeconds == 2.5);
    CHECK(outcome.options.outputPath == "test.iq");
    CHECK(outcome.options.force);
    CHECK(outcome.options.biasT == 1);
    CHECK(outcome.options.centerHz == 100.25);
    CHECK(outcome.options.sampleRateSps == 2000000.5);
}

void testDefaultsAndHelp() {
    const auto defaults = parse({"capture", "--duration", "1"});
    CHECK(defaults.result == rsp1b::ParseResult::success);
    CHECK(defaults.options.centerHz == rsp1b::kDefaultCenterHz);
    CHECK(defaults.options.sampleRateSps == rsp1b::kDefaultSampleRateSps);
    CHECK(defaults.options.biasT == 0);
    CHECK(!defaults.options.force);
    CHECK(defaults.options.outputPath.extension() == ".iq");

    const auto forced = parse({"capture", "--duration", "1", "--force"});
    CHECK(forced.result == rsp1b::ParseResult::success);
    CHECK(forced.options.force);

    const auto longHelp = parse({"capture", "--help"});
    CHECK(longHelp.result == rsp1b::ParseResult::help);
    CHECK_CONTAINS(longHelp.message, "Usage:");
    CHECK_CONTAINS(longHelp.message, "--force");

    const auto shortHelp = parse({"capture", "-h"});
    CHECK(shortHelp.result == rsp1b::ParseResult::help);
}

void testInvalidOptions() {
    for (const char* option : {"--duration", "--out", "--bias-t", "--center", "--sample-rate"}) {
        CHECK(parse({"capture", option}).result == rsp1b::ParseResult::error);
    }
    CHECK(parse({"capture", "--unknown"}).result == rsp1b::ParseResult::error);
    CHECK(parse({"capture"}).result == rsp1b::ParseResult::error);

    for (const char* value : {"0", "-1", "nan", "inf", "1e9999", "1e-9999"}) {
        CHECK(parse({"capture", "--duration", value}).result == rsp1b::ParseResult::error);
    }
    for (const char* value : {"0", "-1", "nan", "inf", "1e9999", "1e-9999"}) {
        CHECK(parse({"capture", "--duration", "1", "--sample-rate", value}).result ==
              rsp1b::ParseResult::error);
        CHECK(parse({"capture", "--duration", "1", "--center", value}).result ==
              rsp1b::ParseResult::error);
    }
    for (const char* value : {"-1", "2", "01", "true"}) {
        CHECK(parse({"capture", "--duration", "1", "--bias-t", value}).result ==
              rsp1b::ParseResult::error);
    }
}

}  // namespace

int main() {
    testValidOptions();
    testDefaultsAndHelp();
    testInvalidOptions();
    return test_support::failures == 0 ? 0 : 1;
}
