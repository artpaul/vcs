#pragma once

#include "platform.h"

#include <filesystem>

#if defined(_unix_)
#   define FHANDLE int
#elif defined(_win_)
#   define FHANDLE HANDLE
#endif

class File {
public:
    File();
    explicit File(FHANDLE fd);
    File(File&& other);
    ~File();

    /**
     * Open file for write in append mode.
     */
    static File ForAppend(const std::filesystem::path& path, bool create = true);

    /**
     * Open file for read.
     */
    static File ForRead(const std::filesystem::path& path, bool follow = true);

    /**
     * Open file for read.
     */
    static File ForOverwrite(const std::filesystem::path& path);

public:
    FHANDLE Handle() const;

    bool Valid() const;

public:
    int Close();

    void FlushData();

    /** Loads data at the current position. */
    [[nodiscard]] size_t Load(void* buf, size_t len);

    /** Loads data at the given offset. */
    [[nodiscard]] size_t Load(void* buf, size_t len, off_t offset) const;

    /** Reads data at the current position. */
    [[nodiscard]] size_t Read(void* buf, size_t len);

    /** Reads data at a given offset. */
    [[nodiscard]] size_t Read(void* buf, size_t len, off_t offset) const;

    size_t Write(const void* buf, size_t len);

    /** Current size of the file. */
    [[nodiscard]] size_t Size() const;

private:
    File(const File&) = delete;

    File& operator=(const File&) = delete;

private:
    FHANDLE fd_;
};

class FileMap {
public:
    explicit FileMap(File& file);

    ~FileMap();

    [[nodiscard]] void* Map();

    [[nodiscard]] size_t Size() const;

private:
    File& file_;
    void* data_;
    size_t size_;
};

void StringToFile(const std::filesystem::path& path, const std::string_view value);

std::string StringFromFile(const std::filesystem::path& path, bool stripped = false);
