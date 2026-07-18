#include "timestamp.hpp"

#include <array>

namespace rsp1b {
namespace {

bool localTime(std::time_t time, std::tm& result) noexcept {
#if defined(_WIN32)
    return localtime_s(&result, &time) == 0;
#else
    return localtime_r(&time, &result) != nullptr;
#endif
}

}  // namespace

std::string formatLocalTimestamp(std::time_t time, const char* format) {
    if (format == nullptr) {
        return {};
    }

    std::tm local{};
    if (!localTime(time, local)) {
        return {};
    }

    std::array<char, 64> buffer{};
    const std::size_t length = std::strftime(buffer.data(), buffer.size(), format, &local);
    if (length == 0) {
        return {};
    }
    return std::string(buffer.data(), length);
}

std::string localTimestampForName() {
    const std::time_t now = std::time(nullptr);
    if (now == static_cast<std::time_t>(-1)) {
        return {};
    }
    return formatLocalTimestamp(now, "%Y%m%d_%H%M%S");
}

std::string localTimestampMetadata() {
    const std::time_t now = std::time(nullptr);
    if (now == static_cast<std::time_t>(-1)) {
        return {};
    }
    return formatLocalTimestamp(now, "%Y-%m-%d %H:%M:%S %Z");
}

}  // namespace rsp1b
