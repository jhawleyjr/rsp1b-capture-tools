#include "iq_writer.hpp"

#include "output_file.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

class BlockingFailingStreamBuffer : public std::streambuf {
public:
    void waitUntilWriting() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return writing_; });
    }

    void releaseFailure() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

protected:
    std::streamsize xsputn(const char*, std::streamsize) override {
        std::unique_lock<std::mutex> lock(mutex_);
        writing_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
        return 0;
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

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void testOrderingAndDrain() {
    TemporaryDirectory temporaryDirectory;
    const auto outputPath = temporaryDirectory.path() / "ordered.iq";
    std::atomic<bool> stopRequested{false};
    std::string error;
    auto writer = rsp1b::IqWriter::openFile(outputPath, false, 8, &stopRequested, error);
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
    const auto statistics = writer.statistics();
    CHECK(statistics.writerFailure);
    CHECK(statistics.samplesAccepted == 1);
    CHECK(statistics.samplesWritten == 0);
    CHECK(statistics.droppedBlockCount == 1);
    CHECK(stopRequested.load());
    CHECK(!writer.failureMessage().empty());
}

void testQueuedBlocksCountedAfterWriteFailure() {
    BlockingFailingStreamBuffer buffer;
    auto output = std::make_unique<std::ostream>(&buffer);
    std::atomic<bool> stopRequested{false};
    rsp1b::IqWriter writer(std::move(output), 3, &stopRequested);

    CHECK(writer.enqueue({1, 2}) == rsp1b::EnqueueResult::accepted);
    buffer.waitUntilWriting();
    CHECK(writer.enqueue({3, 4, 5, 6}) == rsp1b::EnqueueResult::accepted);
    CHECK(writer.enqueue({7, 8}) == rsp1b::EnqueueResult::accepted);
    buffer.releaseFailure();

    CHECK(!writer.finish());
    CHECK(!writer.finish());
    const auto statistics = writer.statistics();
    CHECK(statistics.writerFailure);
    CHECK(statistics.droppedBlockCount == 3);
    CHECK(statistics.samplesAccepted == 4);
    CHECK(statistics.samplesWritten == 0);
    CHECK(stopRequested.load());
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
        temporaryDirectory.path() / "empty.iq", false, 2, nullptr, error);
    CHECK(writer != nullptr);
    CHECK(writer->finish());
    CHECK(writer->statistics().samplesWritten == 0);
}

void testFileOverwriteProtection() {
    TemporaryDirectory temporaryDirectory;
    const auto existingPath = temporaryDirectory.path() / "existing.iq";
    {
        std::ofstream sentinel(existingPath, std::ios::binary);
        sentinel << "sentinel contents";
    }

    std::string error;
    auto rejected = rsp1b::IqWriter::openFile(existingPath, false, 2, nullptr, error);
    CHECK(rejected == nullptr);
    CHECK_CONTAINS(error, "existing file was not modified");
    CHECK_CONTAINS(error, "--force");
    CHECK(readText(existingPath) == "sentinel contents");

    error.clear();
    auto forced = rsp1b::IqWriter::openFile(existingPath, true, 2, nullptr, error);
    CHECK(forced != nullptr);
    CHECK(forced->finish());
    CHECK(std::filesystem::file_size(existingPath) == 0);

    const auto newPath = temporaryDirectory.path() / "new.iq";
    auto created = rsp1b::IqWriter::openFile(newPath, false, 2, nullptr, error);
    CHECK(created != nullptr);
    CHECK(created->enqueue({9, 10}) == rsp1b::EnqueueResult::accepted);
    CHECK(created->finish());
    CHECK(readSamples(newPath) == std::vector<std::int16_t>({9, 10}));
}

void testForcedOutputRollbackBeforeInitialization() {
    TemporaryDirectory temporaryDirectory;
    const auto outputPath = temporaryDirectory.path() / "preserved.iq";
    {
        std::ofstream sentinel(outputPath, std::ios::binary);
        sentinel << "original recording";
    }

    std::string error;
    rsp1b::ExistingFileRollback rollback;
    CHECK(rollback.preserve(outputPath, true, error));
    CHECK(rollback.active());
    CHECK(!std::filesystem::exists(outputPath));

    auto writer = rsp1b::IqWriter::openFile(outputPath, true, 2, nullptr, error);
    CHECK(writer != nullptr);
    CHECK(writer->finish());
    CHECK(rollback.restore(error));
    CHECK(!rollback.active());
    CHECK(readText(outputPath) == "original recording");

    CHECK(rollback.preserve(outputPath, true, error));
    {
        std::ofstream replacement(outputPath, std::ios::binary);
        replacement << "authorized replacement";
    }
    CHECK(rollback.commit(error));
    CHECK(!rollback.active());
    CHECK(readText(outputPath) == "authorized replacement");
}

}  // namespace

int main() {
    testOrderingAndDrain();
    testWriteFailure();
    testQueuedBlocksCountedAfterWriteFailure();
    testQueueOverflow();
    testEmptyStop();
    testFileOverwriteProtection();
    testForcedOutputRollbackBeforeInitialization();
    return test_support::failures == 0 ? 0 : 1;
}
