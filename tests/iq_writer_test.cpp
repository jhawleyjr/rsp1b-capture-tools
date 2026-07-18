#include "iq_writer.hpp"

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("rsp1b_writer_test_" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class FailingStreamBuffer : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize) override {
        return 0;
    }

    int overflow(int) override {
        return traits_type::eof();
    }
};

class BlockingStreamBuffer : public std::streambuf {
public:
    void waitUntilWriting() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return writing_; });
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        std::unique_lock<std::mutex> lock(mutex_);
        writing_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
        return count;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool writing_ = false;
    bool released_ = false;
};

std::vector<std::int16_t> readSamples(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const std::streamoff bytes = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::int16_t> samples(static_cast<std::size_t>(bytes) / sizeof(std::int16_t));
    input.read(reinterpret_cast<char*>(samples.data()), bytes);
    return samples;
}

void testOrderingAndDrain() {
    TemporaryDirectory temporaryDirectory;
    const auto outputPath = temporaryDirectory.path() / "ordered.iq";
    std::atomic<bool> stopRequested{false};
    std::string error;
    auto writer = rsp1b::IqWriter::openFile(outputPath, 8, &stopRequested, error);
    CHECK(writer != nullptr);
    CHECK(writer->enqueue({1, 2, 3, 4}) == rsp1b::EnqueueResult::accepted);
    CHECK(writer->enqueue({5, 6}) == rsp1b::EnqueueResult::accepted);
    CHECK(writer->finish());
    CHECK(readSamples(outputPath) == std::vector<std::int16_t>({1, 2, 3, 4, 5, 6}));
    const auto statistics = writer->statistics();
    CHECK(statistics.samplesAccepted == 3);
    CHECK(statistics.samplesWritten == 3);
    CHECK(!stopRequested.load());
    CHECK(writer->enqueue({7, 8}) == rsp1b::EnqueueResult::closed);
}

void testWriteFailure() {
    FailingStreamBuffer buffer;
    auto output = std::make_unique<std::ostream>(&buffer);
    std::atomic<bool> stopRequested{false};
    rsp1b::IqWriter writer(std::move(output), 2, &stopRequested);
    CHECK(writer.enqueue({1, 2}) == rsp1b::EnqueueResult::accepted);
    CHECK(!writer.finish());
    CHECK(writer.statistics().writerFailure);
    CHECK(stopRequested.load());
    CHECK(!writer.failureMessage().empty());
}

void testQueueOverflow() {
    BlockingStreamBuffer buffer;
    auto output = std::make_unique<std::ostream>(&buffer);
    std::atomic<bool> stopRequested{false};
    rsp1b::IqWriter writer(std::move(output), 1, &stopRequested);
    CHECK(writer.enqueue({1, 2}) == rsp1b::EnqueueResult::accepted);
    buffer.waitUntilWriting();
    CHECK(writer.enqueue({3, 4}) == rsp1b::EnqueueResult::accepted);
    CHECK(writer.enqueue({5, 6}) == rsp1b::EnqueueResult::queueFull);
    CHECK(stopRequested.load());
    buffer.release();
    CHECK(writer.finish());
    const auto statistics = writer.statistics();
    CHECK(statistics.queueOverflowCount == 1);
    CHECK(statistics.droppedBlockCount == 1);
    CHECK(statistics.samplesAccepted == 2);
    CHECK(statistics.samplesWritten == 2);
}

void testEmptyStop() {
    TemporaryDirectory temporaryDirectory;
    std::string error;
    auto writer = rsp1b::IqWriter::openFile(
        temporaryDirectory.path() / "empty.iq", 2, nullptr, error);
    CHECK(writer != nullptr);
    CHECK(writer->finish());
    CHECK(writer->statistics().samplesWritten == 0);
}

}  // namespace

int main() {
    testOrderingAndDrain();
    testWriteFailure();
    testQueueOverflow();
    testEmptyStop();
    return test_support::failures == 0 ? 0 : 1;
}
