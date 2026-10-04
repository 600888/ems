#include "net/protocol/dynamic_library.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>
#include <utility>

namespace ems::net::protocol {

class DynamicLibrary::Impl {
   public:
#ifdef _WIN32
    HMODULE handle{nullptr};
#else
    void* handle{nullptr};
#endif
    std::string error;

    ~Impl() { close(); }

    bool close() noexcept {
        if (!handle) return true;
#ifdef _WIN32
        if (!FreeLibrary(handle)) {
            const auto code = GetLastError();
            try {
                error = "FreeLibrary failed: " + std::to_string(code);
            } catch (...) {
            }
            return false;
        }
#else
        if (dlclose(handle) != 0) {
            const char* detail = dlerror();
            try {
                error = detail ? detail : "dlclose failed";
            } catch (...) {
            }
            return false;
        }
#endif
        handle = nullptr;
        return true;
    }
};

DynamicLibrary::DynamicLibrary() : impl_(std::make_unique<Impl>()) {}

DynamicLibrary::~DynamicLibrary() = default;
DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept = default;
DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept = default;

bool DynamicLibrary::open(const std::string& path) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->handle) {
        impl_->error = "Library is already open";
        return false;
    }
    try {
        const auto absolute = std::filesystem::absolute(std::filesystem::u8path(path));
#ifdef _WIN32
        impl_->handle =
            LoadLibraryExW(absolute.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!impl_->handle) {
            const auto code = GetLastError();
            impl_->error = "LoadLibraryExW failed: " + std::to_string(code);
        }
#else
        impl_->handle = dlopen(absolute.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!impl_->handle) {
            const char* error = dlerror();
            impl_->error = error ? error : "dlopen failed";
        }
#endif
    } catch (const std::exception& error) {
        impl_->error = error.what();
    }
    if (impl_->handle) impl_->error.clear();
    return impl_->handle != nullptr;
}

void* DynamicLibrary::symbol(const char* name) {
    if (!impl_) return nullptr;
    if (!impl_->handle || !name) {
        impl_->error = "Library is not open or symbol name is null";
        return nullptr;
    }
#ifdef _WIN32
    auto* address = reinterpret_cast<void*>(GetProcAddress(impl_->handle, name));
    if (!address) {
        const auto code = GetLastError();
        impl_->error = "GetProcAddress(" + std::string(name) + ") failed: " + std::to_string(code);
    }
#else
    dlerror();
    void* address = dlsym(impl_->handle, name);
    const char* error = dlerror();
    if (error) {
        impl_->error = error;
        return nullptr;
    }
#endif
    return address;
}

bool DynamicLibrary::close() noexcept { return !impl_ || impl_->close(); }

bool DynamicLibrary::is_open() const noexcept { return impl_ && impl_->handle; }

std::string DynamicLibrary::last_error() const { return impl_ ? impl_->error : std::string{}; }

}  // namespace ems::net::protocol
