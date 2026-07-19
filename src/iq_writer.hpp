#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace rsp1b {

using IqBlock = std::vector<std::int16_t>;

constexpr std::size_t kDefaultMaxQueuedBlocks = 64;

enum class EnqueueResult {
    accepted,
    queueFull,
    closed,
    writerFailed,
};

struct WriterStatistics {
    std::uint64_t samplesAccepted = 0;
    std::uint64_t samplesWritten = 0;
    std::uint64_t queueOverflowCount = 0;
    std::uint64_t droppedBlockCount = 0;
    bool writerFailure = false;
};

class IqWriter {
  public:
    IqWriter(std::unique_ptr<std::ostream> output, std::size_t maxQueuedBlocks,
             std::atomic<bool>* stopRequested = nullptr);
    ~IqWriter() noexcept;

    IqWriter(const IqWriter&) = delete;
    IqWriter& operator=(const IqWriter&) = delete;
    IqWriter(IqWriter&&) = delete;
    IqWriter& operator=(IqWriter&&) = delete;

    static std::unique_ptr<IqWriter> openFile(const std::filesystem::path& path,
                                              bool overwriteAuthorized, std::size_t maxQueuedBlocks,
                                              std::atomic<bool>* stopRequested, std::string& error);

    EnqueueResult enqueue(IqBlock block);
    bool finish() noexcept;
    WriterStatistics statistics() const;
    std::string failureMessage() const;

  private:
    void writerLoop() noexcept;
    void recordWriterFailure(const std::string& message, bool currentBlockWasDropped) noexcept;
    void requestStop() noexcept;

    std::unique_ptr<std::ostream> output_;
    const std::size_t maxQueuedBlocks_;
    std::atomic<bool>* stopRequested_;
    mutable std::mutex mutex_;
    std::condition_variable queueChanged_;
    std::deque<IqBlock> queue_;
    bool accepting_ = true;
    bool closing_ = false;
    bool writerFailure_ = false;
    std::uint64_t samplesAccepted_ = 0;
    std::uint64_t samplesWritten_ = 0;
    std::uint64_t queueOverflowCount_ = 0;
    std::uint64_t droppedBlockCount_ = 0;
    std::string failureMessage_;
    std::thread writerThread_;
};

} // namespace rsp1b
