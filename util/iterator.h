#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <sys/stat.h>

class DirectoryIterator;

class DirectoryEntry {
    friend class DirectoryIterator;

public:
    inline bool is_directory() const noexcept {
        return type_ == std::filesystem::file_type::directory;
    }

    inline bool is_directory_enter() const noexcept {
        return type_ == std::filesystem::file_type::directory && !exit_;
    }

    inline bool is_directory_exit() const noexcept {
        return type_ == std::filesystem::file_type::directory && exit_;
    }

    inline bool is_other() const noexcept {
        return type_ == std::filesystem::file_type::unknown;
    }

    inline bool is_regular_file() const noexcept {
        return type_ == std::filesystem::file_type::regular;
    }

    inline bool is_symlink() const noexcept {
        return type_ == std::filesystem::file_type::symlink;
    }

    inline std::string_view filename() const noexcept {
        return name_;
    }

    inline std::string_view path() const noexcept {
        return path_;
    }

    inline struct stat* status() const noexcept {
        return stat_;
    };

private:
    struct stat* stat_{};
    std::string_view name_;
    std::string_view path_;
    std::filesystem::file_type type_{std::filesystem::file_type::directory};
    bool exit_{false};
};

class DirectoryIterator {
public:
    explicit DirectoryIterator(const std::filesystem::path& path);

    ~DirectoryIterator();

    int Depth() const noexcept;

    const DirectoryEntry* Next();

    bool Status();

    void DisableRecursionPending() noexcept;

    bool RecursionPending() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};
