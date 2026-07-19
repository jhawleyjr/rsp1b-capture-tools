#include "signal_stop.hpp"

#include <csignal>

namespace rsp1b {
namespace {

volatile std::sig_atomic_t stopRequested = 0;

extern "C" void stopSignalHandler(int) {
    stopRequested = 1;
}

} // namespace

bool installStopSignalHandlers(std::string& error) noexcept {
    stopRequested = 0;
    if (std::signal(SIGINT, stopSignalHandler) == SIG_ERR) {
        error = "Unable to install the SIGINT handler.";
        return false;
    }
#ifdef SIGTERM
    if (std::signal(SIGTERM, stopSignalHandler) == SIG_ERR) {
        error = "Unable to install the SIGTERM handler.";
        return false;
    }
#endif
    return true;
}

bool signalStopRequested() noexcept {
    return stopRequested != 0;
}

} // namespace rsp1b
