#include "host_credentials.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "crypto.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#ifdef _WIN32
#include <sddl.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif
namespace ems::web {
namespace {
std::string console_line(const std::string& prompt, bool hidden) {
    std::cout << prompt << std::flush;
#ifdef _WIN32
    auto input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(input, &mode)) throw std::runtime_error("interactive console required");

    struct Restore {
        HANDLE input;
        DWORD mode;
        bool hidden;

        ~Restore() {
            SetConsoleMode(input, mode);
            if (hidden) std::cout << '\n';
        }
    } restore{input, mode, hidden};

    if (!SetConsoleMode(input, hidden ? mode & ~ENABLE_ECHO_INPUT : mode | ENABLE_ECHO_INPUT))
        throw std::runtime_error("console mode");
    wchar_t buffer[1026]{};
    DWORD count = 0;
    if (!ReadConsoleW(input, buffer, 1025, &count, nullptr) || count >= 1025)
        throw std::runtime_error("console read");
    while (count && (buffer[count - 1] == L'\r' || buffer[count - 1] == L'\n')) --count;
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer, int(count), nullptr, 0,
                                   nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("console encoding");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer, int(count), result.data(), size,
                        nullptr, nullptr);
    SecureZeroMemory(buffer, sizeof(buffer));
    return result;
#else
    termios mode{};
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &mode) != 0)
        throw std::runtime_error("interactive terminal required");

    struct Restore {
        termios mode;
        bool hidden;

        ~Restore() {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &mode);
            if (hidden) std::cout << '\n';
        }
    } restore{mode, hidden};

    auto active = mode;
    if (hidden)
        active.c_lflag &= ~ECHO;
    else
        active.c_lflag |= ECHO;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &active) != 0) throw std::runtime_error("terminal mode");
    std::string result;
    std::getline(std::cin, result);
    if (!std::cin || result.size() > 1024) throw std::runtime_error("terminal read");
    return result;
#endif
}
}  // namespace

std::string read_host_line(const std::string& prompt) { return console_line(prompt, false); }

std::string read_host_password(const std::string& prompt) { return console_line(prompt, true); }

void create_identity_key(const std::string& path) {
    if (path.empty()) throw std::invalid_argument("key path required");
    auto value = random_token();
#ifdef _WIN32
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // 受保护 DACL：当前文件所有者、SYSTEM 和本机管理员，禁用父目录继承。
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr))
        throw std::runtime_error("key permissions");
    SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    auto file = CreateFileW(std::filesystem::u8path(path).c_str(), GENERIC_WRITE, 0, &attrs,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("key create");
    DWORD written = 0;
    bool ok = WriteFile(file, value.data(), DWORD(value.size()), &written, nullptr) &&
              written == value.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) throw std::runtime_error("key write");
#else
    int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) throw std::runtime_error("key create");
    bool ok = write(file, value.data(), value.size()) == ssize_t(value.size()) && fsync(file) == 0;
    close(file);
    if (!ok) throw std::runtime_error("key write");
#endif
}
}  // namespace ems::web
