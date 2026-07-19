#include "signal_stop.hpp"

#include "test_support.hpp"

#include <csignal>
#include <string>

int main() {
    std::string error;
    CHECK(rsp1b::installStopSignalHandlers(error));
    CHECK(error.empty());
    CHECK(!rsp1b::signalStopRequested());
    CHECK(std::raise(SIGINT) == 0);
    CHECK(rsp1b::signalStopRequested());
    return test_support::failures == 0 ? 0 : 1;
}
