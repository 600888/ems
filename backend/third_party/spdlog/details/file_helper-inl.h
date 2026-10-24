// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifndef SPDLOG_HEADER_ONLY
    #include <spdlog/details/file_helper.h>
#endif

#include <spdlog/common.h>
#include <spdlog/details/os.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <tuple>

namespace spdlog {
namespace details {

SPDLOG_INLINE file_helper::file_helper(const file_event_handlers &event_handlers)
    : event_handlers_(event_handlers) {}

SPDLOG_INLINE file_helper::~file_helper() { close(); }

SPDLOG_INLINE void file_helper::open(const filename_t &fname, bool truncate) {
    close();
    filename_ = fname;

    auto *mode = SPDLOG_FILENAME_T("ab");
    auto *trunc_mode = SPDLOG_FILENAME_T("wb");

    if (event_handlers_.before_open) {
        event_handlers_.before_open(filename_);
    }

    int attempts = 0;
    int last_errno = 0;
    for (; attempts < open_tries_; ++attempts) {
        last_errno = 0;
        auto dir = os::dir_name(fname);

        // Create directory if not exists and verify it exists
        if (!dir.empty()) {
            if (!os::create_dir(dir)) {
                last_errno = errno;
                details::os::sleep_for_millis(open_interval_);
                continue;
            }
            // Double-check directory exists after creation
            if (!os::path_exists(dir)) {
                last_errno = ENOENT;
                details::os::sleep_for_millis(open_interval_);
                continue;
            }
        }

        if (truncate) {
            // Truncate by opening-and-closing a tmp file in "wb" mode
            std::FILE *tmp;
            if (os::fopen_s(&tmp, fname, trunc_mode)) {
                last_errno = errno;
                // Handle directory not found error specifically
                if (last_errno == ENOENT && !dir.empty()) {
                    if (os::create_dir(dir) && !os::fopen_s(&tmp, fname, trunc_mode)) {
                        std::fclose(tmp);
                        last_errno = 0;
                    }
                }
                if (last_errno != 0) {
                    details::os::sleep_for_millis(open_interval_);
                    continue;
                }
            }
            std::fclose(tmp);
        }

        // Try to open the actual log file in append mode
        if (!os::fopen_s(&fd_, fname, mode)) {
            // Success
            if (event_handlers_.after_open) {
                event_handlers_.after_open(filename_, fd_);
            }
            return;
        }

        last_errno = errno;
        // Handle directory not found error specifically for the main file open
        if (last_errno == ENOENT && !dir.empty()) {
            if (os::create_dir(dir) && !os::fopen_s(&fd_, fname, mode)) {
                if (event_handlers_.after_open) {
                    event_handlers_.after_open(filename_, fd_);
                }
                return;
            }
            last_errno = errno;
        }

        details::os::sleep_for_millis(open_interval_);
    }

    // Failed after all attempts
    std::string error_msg = "Failed opening file " + os::filename_to_str(filename_) + " for writing. Attempts: " + std::to_string(attempts) + ", errno: " + std::to_string(last_errno) + ", error: " + strerror(last_errno);
    throw_spdlog_ex(error_msg, last_errno);
}

SPDLOG_INLINE void file_helper::reopen(bool truncate) {
    if (filename_.empty()) {
        throw_spdlog_ex("Failed re opening file - was not opened before");
    }
    this->open(filename_, truncate);
}

SPDLOG_INLINE void file_helper::flush() {
    if (std::fflush(fd_) != 0) {
        throw_spdlog_ex("Failed flush to file " + os::filename_to_str(filename_), errno);
    }
}

SPDLOG_INLINE void file_helper::sync() {
    if (!os::fsync(fd_)) {
        throw_spdlog_ex("Failed to fsync file " + os::filename_to_str(filename_), errno);
    }
}

SPDLOG_INLINE void file_helper::close() {
    if (fd_ != nullptr) {
        if (event_handlers_.before_close) {
            event_handlers_.before_close(filename_, fd_);
        }

        std::fclose(fd_);
        fd_ = nullptr;

        if (event_handlers_.after_close) {
            event_handlers_.after_close(filename_);
        }
    }
}

SPDLOG_INLINE void file_helper::write(const memory_buf_t &buf) {
    if (fd_ == nullptr) return;
    size_t msg_size = buf.size();
    auto data = buf.data();

    if (!details::os::fwrite_bytes(data, msg_size, fd_)) {
        throw_spdlog_ex("Failed writing to file " + os::filename_to_str(filename_), errno);
    }
}

SPDLOG_INLINE size_t file_helper::size() const {
    if (fd_ == nullptr) {
        throw_spdlog_ex("Cannot use size() on closed file " + os::filename_to_str(filename_));
    }
    return os::filesize(fd_);
}

SPDLOG_INLINE const filename_t &file_helper::filename() const { return filename_; }

//
// return file path and its extension:
//
// "mylog.txt" => ("mylog", ".txt")
// "mylog" => ("mylog", "")
// "mylog." => ("mylog.", "")
// "/dir1/dir2/mylog.txt" => ("/dir1/dir2/mylog", ".txt")
//
// the starting dot in filenames is ignored (hidden files):
//
// ".mylog" => (".mylog". "")
// "my_folder/.mylog" => ("my_folder/.mylog", "")
// "my_folder/.mylog.txt" => ("my_folder/.mylog", ".txt")
SPDLOG_INLINE std::tuple<filename_t, filename_t> file_helper::split_by_extension(
    const filename_t &fname) {
    auto ext_index = fname.rfind('.');

    // no valid extension found - return whole path and empty string as
    // extension
    if (ext_index == filename_t::npos || ext_index == 0 || ext_index == fname.size() - 1) {
        return std::make_tuple(fname, filename_t());
    }

    // treat cases like "/etc/rc.d/somelogfile or "/abc/.hiddenfile"
    auto folder_index = fname.find_last_of(details::os::folder_seps_filename);
    if (folder_index != filename_t::npos && folder_index >= ext_index - 1) {
        return std::make_tuple(fname, filename_t());
    }

    // finally - return a valid base and extension tuple
    return std::make_tuple(fname.substr(0, ext_index), fname.substr(ext_index));
}

}  // namespace details
}  // namespace spdlog
