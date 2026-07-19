#include "iq_writer.hpp"

#include "output_file.hpp"

#include <fstream>
#include <limits>
#include <utility>

static_assert(sizeof(std::int16_t) == 2, "IQ samples require a 16-bit representation");

#if !defined(_WIN32) &&                                                                    \
    (!defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "RSP1B Capture Tools currently supports little-endian targets only"
#endif

namespace rsp1b {

IqWriter::IqWriter(std::unique_ptr<std::ostream> output,
                   std::size_t maxQueuedBlocks,
                   std::atomic<bool>* stopRequested)
    : output_(std::move(output)),
      maxQueuedBlocks_(maxQueuedBlocks),
      stopRequested_(stopRequested) {
    if (output_ == nullptr || maxQueuedBlocks_ == 0) {
        accepting_ = false;
        closing_ = true;
        writerFailure_ = true;
        failureMessage_ = output_ == nullptr ? "Writer output stream is unavailable."
                                             : "Writer queue limit must be greater than zero.";
        requestStop();
        return;
    }
    writerThread_ = std::thread(&IqWriter::writerLoop, this);
}

IqWriter::~IqWriter() noexcept {
    finish();
}

std::unique_ptr<IqWriter> IqWriter::openFile(const std::filesystem::path& path,
                                             bool overwriteAuthorized,
                                             std::size_t maxQueuedBlocks,
                                             std::atomic<bool>* stopRequested,
                                             std::string& error) {
    bool pathExisted = false;
    if (!checkOutputPath(path, overwriteAuthorized, "IQ output", pathExisted, error)) {
        return nullptr;
    }

    auto output = std::make_unique<std::ofstream>(
        path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!*output) {
        error = "Unable to open IQ output: " + path.string();
        return nullptr;
    }
    return std::make_unique<IqWriter>(std::move(output), maxQueuedBlocks, stopRequested);
}

EnqueueResult IqWriter::enqueue(IqBlock block) {
    const std::uint64_t complexSamples = static_cast<std::uint64_t>(block.size() / 2U);
    std::lock_guard<std::mutex> lock(mutex_);
    if (writerFailure_) {
        ++droppedBlockCount_;
        return EnqueueResult::writerFailed;
    }
    if (!accepting_) {
        ++droppedBlockCount_;
        return EnqueueResult::closed;
    }
    if (queue_.size() >= maxQueuedBlocks_) {
        ++queueOverflowCount_;
        ++droppedBlockCount_;
        accepting_ = false;
        closing_ = true;
        failureMessage_ = "IQ writer queue reached its bounded block limit.";
        requestStop();
        queueChanged_.notify_all();
        return EnqueueResult::queueFull;
    }

    samplesAccepted_ += complexSamples;
    queue_.push_back(std::move(block));
    queueChanged_.notify_one();
    return EnqueueResult::accepted;
}

bool IqWriter::finish() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        closing_ = true;
    }
    queueChanged_.notify_all();

    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    output_.reset();
    return !writerFailure_;
}

WriterStatistics IqWriter::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    WriterStatistics statistics;
    statistics.samplesAccepted = samplesAccepted_;
    statistics.samplesWritten = samplesWritten_;
    statistics.queueOverflowCount = queueOverflowCount_;
    statistics.droppedBlockCount = droppedBlockCount_;
    statistics.writerFailure = writerFailure_;
    return statistics;
}

std::string IqWriter::failureMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failureMessage_;
}

void IqWriter::writerLoop() noexcept {
    for (;;) {
        IqBlock block;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queueChanged_.wait(lock, [&] { return closing_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (closing_) {
                    break;
                }
                continue;
            }
            block = std::move(queue_.front());
            queue_.pop_front();
        }

        if (!block.empty()) {
            const auto byteCount = block.size() * sizeof(std::int16_t);
            if (byteCount > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
                recordWriterFailure("IQ block is too large for the output stream.", true);
                break;
            }
            output_->write(reinterpret_cast<const char*>(block.data()),
                           static_cast<std::streamsize>(byteCount));
        }

        if (!*output_) {
            recordWriterFailure("IQ output stream failed while writing.", true);
            break;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            samplesWritten_ += static_cast<std::uint64_t>(block.size() / 2U);
        }
    }

    output_->flush();
    if (!*output_) {
        recordWriterFailure("IQ output stream failed while flushing.", false);
    }
}

void IqWriter::recordWriterFailure(const std::string& message,
                                   bool currentBlockWasDropped) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!writerFailure_) {
        droppedBlockCount_ += static_cast<std::uint64_t>(queue_.size());
        if (currentBlockWasDropped) {
            ++droppedBlockCount_;
        }
        queue_.clear();
        failureMessage_ = message;
    }
    writerFailure_ = true;
    accepting_ = false;
    closing_ = true;
    requestStop();
}

void IqWriter::requestStop() noexcept {
    if (stopRequested_ != nullptr) {
        stopRequested_->store(true, std::memory_order_relaxed);
    }
}

}  // namespace rsp1b
