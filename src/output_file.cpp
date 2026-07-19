#include "output_file.hpp"

#include <string_view>
#include <system_error>

namespace rsp1b {
namespace {

constexpr unsigned int kMaximumBackupSuffix = 1000;

std::string refusedReplacementMessage(const std::filesystem::path& path,
                                      const std::string& outputDescription,
                                      std::string_view reason) {
    return "Refusing to replace " + outputDescription + " '" + path.string() + "' because it " +
           std::string(reason) + "; --force only replaces existing regular files.";
}

bool pathsEqualForPlatform(const std::filesystem::path& left,
                           const std::filesystem::path& right) {
    const std::string leftText = left.generic_string();
    const std::string rightText = right.generic_string();
#if defined(_WIN32) || defined(__APPLE__)
    if (leftText.size() != rightText.size()) {
        return false;
    }
    for (std::size_t index = 0; index < leftText.size(); ++index) {
        const auto foldAscii = [](char character) {
            return character >= 'A' && character <= 'Z'
                       ? static_cast<char>(character - 'A' + 'a')
                       : character;
        };
        if (foldAscii(leftText[index]) != foldAscii(rightText[index])) {
            return false;
        }
    }
    return true;
#else
    return leftText == rightText;
#endif
}

bool normalizedPath(const std::filesystem::path& path,
                    std::filesystem::path& normalized,
                    std::string& error) {
    std::error_code filesystemError;
    normalized = std::filesystem::weakly_canonical(path, filesystemError);
    if (filesystemError) {
        error = "Unable to normalize output path '" + path.string() +
                "' while checking for an IQ/metadata alias: " + filesystemError.message();
        return false;
    }
    return true;
}

}  // namespace

bool checkOutputPath(const std::filesystem::path& path,
                     bool overwriteAuthorized,
                     const std::string& outputDescription,
                     bool& pathExisted,
                     std::string& error) {
    error.clear();
    std::error_code filesystemError;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, filesystemError);
    if (filesystemError && status.type() != std::filesystem::file_type::not_found) {
        error = "Unable to check " + outputDescription + " path '" + path.string() +
                "': " + filesystemError.message();
        return false;
    }
    filesystemError.clear();
    pathExisted = status.type() != std::filesystem::file_type::not_found;
    if (std::filesystem::is_symlink(status)) {
        error = refusedReplacementMessage(path, outputDescription, "is a symbolic link");
        return false;
    }
    if (pathExisted && !std::filesystem::is_regular_file(status)) {
        error = refusedReplacementMessage(path, outputDescription, "is not a regular file");
        return false;
    }
    if (pathExisted && !overwriteAuthorized) {
        error = "Refusing to overwrite existing " + outputDescription + " '" + path.string() +
                "'. The existing file was not modified; use --force to overwrite it.";
        return false;
    }
    return true;
}

bool validateDistinctOutputPaths(const std::filesystem::path& iqPath,
                                 const std::filesystem::path& metadataPath,
                                 std::string& error) {
    error.clear();
    std::filesystem::path normalizedIqPath;
    std::filesystem::path normalizedMetadataPath;
    if (!normalizedPath(iqPath, normalizedIqPath, error) ||
        !normalizedPath(metadataPath, normalizedMetadataPath, error)) {
        return false;
    }

    bool alias = pathsEqualForPlatform(normalizedIqPath, normalizedMetadataPath);
    if (!alias) {
        std::error_code iqError;
        std::error_code metadataError;
        const bool iqExists = std::filesystem::exists(iqPath, iqError);
        const bool metadataExists = std::filesystem::exists(metadataPath, metadataError);
        if (iqError || metadataError) {
            const std::error_code& filesystemError = iqError ? iqError : metadataError;
            error = "Unable to compare IQ and metadata output paths: " +
                    filesystemError.message();
            return false;
        }
        if (iqExists && metadataExists) {
            std::error_code equivalentError;
            alias = std::filesystem::equivalent(iqPath, metadataPath, equivalentError);
            if (equivalentError) {
                error = "Unable to compare existing IQ and metadata output paths: " +
                        equivalentError.message();
                return false;
            }
        }
    }

    if (alias) {
        error = "Refusing capture because IQ output '" + iqPath.string() +
                "' and metadata output '" + metadataPath.string() +
                "' resolve to the same path; --force cannot authorize metadata to replace IQ data.";
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

    std::error_code filesystemError;
    const std::filesystem::file_status originalStatus =
        std::filesystem::symlink_status(path, filesystemError);
    if (filesystemError && originalStatus.type() != std::filesystem::file_type::not_found) {
        error = "Unable to check existing IQ output '" + path.string() +
                "' before preserving it: " + filesystemError.message();
        return false;
    }
    filesystemError.clear();
    if (std::filesystem::is_symlink(originalStatus)) {
        error = refusedReplacementMessage(path, "existing IQ output", "is a symbolic link");
        return false;
    }
    if (!std::filesystem::is_regular_file(originalStatus)) {
        error = refusedReplacementMessage(path, "existing IQ output", "is not a regular file");
        return false;
    }

    originalPath_ = path;
    backupPath_ = path;
    backupPath_ += ".rsp1b-backup";
    for (unsigned int suffix = 0;; ++suffix) {
        const std::filesystem::file_status backupStatus =
            std::filesystem::symlink_status(backupPath_, filesystemError);
        if (filesystemError && backupStatus.type() != std::filesystem::file_type::not_found) {
            error = "Unable to check temporary backup path '" + backupPath_.string() +
                    "': " + filesystemError.message();
            return false;
        }
        filesystemError.clear();
        if (backupStatus.type() == std::filesystem::file_type::not_found) {
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
