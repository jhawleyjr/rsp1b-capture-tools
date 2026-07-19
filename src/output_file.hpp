#pragma once

#include <filesystem>
#include <string>

namespace rsp1b {

bool checkOutputPath(const std::filesystem::path& path, bool overwriteAuthorized,
                     const std::string& outputDescription, bool& pathExisted, std::string& error);

bool validateDistinctOutputPaths(const std::filesystem::path& iqPath,
                                 const std::filesystem::path& metadataPath, std::string& error);

class ExistingFileRollback {
  public:
    ExistingFileRollback() = default;
    ~ExistingFileRollback() noexcept;

    ExistingFileRollback(const ExistingFileRollback&) = delete;
    ExistingFileRollback& operator=(const ExistingFileRollback&) = delete;
    ExistingFileRollback(ExistingFileRollback&&) = delete;
    ExistingFileRollback& operator=(ExistingFileRollback&&) = delete;

    bool preserve(const std::filesystem::path& path, bool preserveExistingFile, std::string& error);
    bool commit(std::string& error);
    bool restore(std::string& error);
    [[nodiscard]] bool active() const noexcept;

  private:
    std::filesystem::path originalPath_;
    std::filesystem::path backupPath_;
    bool active_ = false;
};

} // namespace rsp1b
