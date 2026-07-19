#pragma once

#include <ctime>
#include <string>

namespace rsp1b {

std::string formatLocalTimestamp(std::time_t time, const char* format);
std::string localTimestampForName();
std::string localTimestampMetadata();

}  // namespace rsp1b
