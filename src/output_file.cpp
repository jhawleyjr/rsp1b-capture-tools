#include "output_file.hpp"

#include <system_error>

namespace rsp1b {
namespace {

constexpr unsigned int kMaximumBackupSuffix = 1000;

}  // namespace

bool checkOutputPath(const std::filesystem::path& path,
                     bool overwriteAuthorized,
                     const std::string& outputDescription,
                     bool& pathExisted,
                     std::string& error) {
    error.clear();
    std::error_code filesystemError;
    pathExisted = std::filesystem::exists(path, filesystemError);
    if (filesystemError) {
        error = "Unable to check " + outputDescription + " path '" + path.string() +
                "': " + filesystemError.message();
        return false;
    }
    if (pathExisted && !overwriteAuthorized) {
        error = "Refusing to overwrite existing " + outputDescription + " '" + path.string() +
                "'. The existing file was not modified; use --force to overwrite it.";
        return false;
    }
    return true;
}

ExistingFileRollback::~ExistingFileRollback() noexcept {
    if (!active_) {
        return;
    }

    std::error_code filesystemError;
    std::filesystem::remove(originalPath_, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(backupPath_, originalPath_, filesystemError);
}

bool ExistingFileRollback::preserve(const std::filesystem::path& path,
                                    bool preserveExistingFile,
                                    std::string& error) {
    error.clear();
    if (!preserveExistingFile) {
        return true;
    }
    if (active_) {
        error = "An existing output file is already being preserved.";
        return false;
    }

    originalPath_ = path;
    backupPath_ = path;
    backupPath_ += ".rsp1b-backup";
    std::error_code filesystemError;
    for (unsigned int suffix = 0;; ++suffix) {
        const bool backupExists = std::filesystem::exists(backupPath_, filesystemError);
        if (filesystemError) {
            error = "Unable to check temporary backup path '" + backupPath_.string() +
                    "': " + filesystemError.message();
            return false;
        }
        if (!backupExists) {
            break;
        }
        if (suffix == kMaximumBackupSuffix) {
            error = "Unable to reserve a temporary backup path for '" + path.string() + "'.";
            return false;
        }
        backupPath_ = path;
        backupPath_ += ".rsp1b-backup." + std::to_string(suffix + 1U);
    }

    std::filesystem::rename(originalPath_, backupPath_, filesystemError);
    if (filesystemError) {
        error = "Unable to preserve existing IQ output '" + originalPath_.string() +
                "' before receiver initialization: " + filesystemError.message();
        return false;
    }
    active_ = true;
    return true;
}

bool ExistingFileRollback::commit(std::string& error) {
    error.clear();
    if (!active_) {
        return true;
    }

    std::error_code filesystemError;
    const bool removed = std::filesystem::remove(backupPath_, filesystemError);
    if (filesystemError || !removed) {
        error = "Unable to remove preserved IQ output backup '" + backupPath_.string() + "'";
        if (filesystemError) {
            error += ": " + filesystemError.message();
        }
        return false;
    }
    active_ = false;
    return true;
}

bool ExistingFileRollback::restore(std::string& error) {
    error.clear();
    if (!active_) {
        return true;
    }

    std::error_code filesystemError;
    std::filesystem::remove(originalPath_, filesystemError);
    if (filesystemError) {
        error = "Unable to remove uninitialized replacement IQ output '" + originalPath_.string() +
                "': " + filesystemError.message();
        return false;
    }
    std::filesystem::rename(backupPath_, originalPath_, filesystemError);
    if (filesystemError) {
        error = "Unable to restore preserved IQ output '" + originalPath_.string() +
                "': " + filesystemError.message();
        return false;
    }
    active_ = false;
    return true;
}

bool ExistingFileRollback::active() const noexcept {
    return active_;
}

}  // namespace rsp1b
