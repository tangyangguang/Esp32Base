#pragma once

#include "Arduino.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace native_littlefs {

struct Entry {
    bool directory = false;
    std::vector<uint8_t> data;
};

inline std::map<std::string, Entry>& entries() {
    static std::map<std::string, Entry> value;
    return value;
}

inline bool& mounted() {
    static bool value = false;
    return value;
}

inline void reset() {
    entries().clear();
    entries()["/"].directory = true;
    mounted() = false;
}

}  // namespace native_littlefs

class File {
public:
    File() = default;

    File(const std::string& path, const char* mode)
        : path_(path),
          mode_(mode ? mode : "r"),
          valid_(true) {
        if (!mode_.empty() && mode_[0] == 'a') {
            position_ = native_littlefs::entries()[path_].data.size();
        }
    }

    explicit operator bool() const {
        return valid_;
    }

    bool isDirectory() const {
        const auto it = native_littlefs::entries().find(path_);
        return valid_ && it != native_littlefs::entries().end() && it->second.directory;
    }

    size_t position() const {
        return position_;
    }

    size_t size() const {
        const auto it = native_littlefs::entries().find(path_);
        return valid_ && it != native_littlefs::entries().end() ? it->second.data.size() : 0;
    }

    size_t read(uint8_t* out, size_t length) {
        if (!valid_ || !out || isDirectory()) {
            return 0;
        }
        const auto& data = native_littlefs::entries()[path_].data;
        const size_t available = position_ < data.size() ? data.size() - position_ : 0;
        const size_t count = std::min(length, available);
        if (count > 0) {
            std::memcpy(out, data.data() + position_, count);
            position_ += count;
        }
        return count;
    }

    size_t write(const uint8_t* data, size_t length) {
        if (!valid_ || (!data && length > 0) || isDirectory() ||
            (mode_.find('w') == std::string::npos &&
             mode_.find('a') == std::string::npos &&
             mode_.find('+') == std::string::npos)) {
            return 0;
        }
        auto& bytes = native_littlefs::entries()[path_].data;
        if (!mode_.empty() && mode_[0] == 'a') {
            position_ = bytes.size();
        }
        if (position_ + length > bytes.size()) {
            bytes.resize(position_ + length);
        }
        if (length > 0) {
            std::memcpy(bytes.data() + position_, data, length);
            position_ += length;
        }
        return length;
    }

    bool seek(uint32_t offset, SeekMode mode) {
        if (!valid_) {
            return false;
        }
        size_t target = offset;
        if (mode == SeekCur) {
            target = position_ + offset;
        } else if (mode == SeekEnd) {
            target = size() + offset;
        }
        if (target > size()) {
            return false;
        }
        position_ = target;
        return true;
    }

    void flush() {}

    void close() {
        valid_ = false;
    }

    const char* name() const {
        return path_.c_str();
    }

    uint32_t getLastWrite() const {
        return 0;
    }

    File openNextFile(const char* = "r") {
        return File();
    }

private:
    std::string path_;
    std::string mode_;
    size_t position_ = 0;
    bool valid_ = false;
};

class NativeLittleFS {
public:
    bool begin(bool) {
        native_littlefs::mounted() = true;
        if (native_littlefs::entries().empty()) {
            native_littlefs::entries()["/"].directory = true;
        }
        return true;
    }

    void end() {
        native_littlefs::mounted() = false;
    }

    bool format() {
        native_littlefs::reset();
        return true;
    }

    File open(const char* path, const char* mode = "r", const bool create = false) {
        if (!native_littlefs::mounted() || !path || path[0] != '/') {
            return File();
        }
        auto it = native_littlefs::entries().find(path);
        if (it == native_littlefs::entries().end()) {
            if (!create) {
                return File();
            }
            it = native_littlefs::entries().emplace(path, native_littlefs::Entry{}).first;
        }
        if (it->second.directory && mode && std::strcmp(mode, "r") != 0) {
            return File();
        }
        if (mode && mode[0] == 'w') {
            it->second.data.clear();
        }
        return File(path, mode);
    }

    bool exists(const char* path) const {
        return native_littlefs::mounted() && path &&
               native_littlefs::entries().find(path) != native_littlefs::entries().end();
    }

    bool remove(const char* path) {
        const auto it = path ? native_littlefs::entries().find(path) : native_littlefs::entries().end();
        if (it == native_littlefs::entries().end() || it->second.directory) {
            return false;
        }
        native_littlefs::entries().erase(it);
        return true;
    }

    bool rename(const char* from, const char* to) {
        if (!from || !to || exists(to)) {
            return false;
        }
        const auto it = native_littlefs::entries().find(from);
        if (it == native_littlefs::entries().end()) {
            return false;
        }
        native_littlefs::entries()[to] = it->second;
        native_littlefs::entries().erase(it);
        return true;
    }

    bool mkdir(const char* path) {
        if (!path || exists(path)) {
            return false;
        }
        native_littlefs::entries()[path].directory = true;
        return true;
    }

    bool rmdir(const char* path) {
        const auto it = path ? native_littlefs::entries().find(path) : native_littlefs::entries().end();
        if (it == native_littlefs::entries().end() || !it->second.directory || std::strcmp(path, "/") == 0) {
            return false;
        }
        native_littlefs::entries().erase(it);
        return true;
    }

    size_t totalBytes() const {
        return 1024U * 1024U;
    }

    size_t usedBytes() const {
        size_t used = 0;
        for (const auto& item : native_littlefs::entries()) {
            used += item.second.data.size();
        }
        return used;
    }
};

static NativeLittleFS LittleFS;
