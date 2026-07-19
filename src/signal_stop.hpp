#pragma once

#include <string>

namespace rsp1b {

bool installStopSignalHandlers(std::string& error) noexcept;
bool signalStopRequested() noexcept;

}  // namespace rsp1b
