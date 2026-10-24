#include "crypto.h"

#include <array>
#include <sstream>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#ifdef _WIN32
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif
namespace ems::web {
namespace {
std::string hex(const unsigned char* p, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}

std::string derive(const std::string& password, const std::string& salt, unsigned long iterations) {
    // 平台密码学提供者执行固定成本派生，不在业务层自行实现哈希轮函数。
    std::array<unsigned char, 32> out{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        throw std::runtime_error("crypto");
    auto status = BCryptDeriveKeyPBKDF2(alg, (PUCHAR)password.data(), (ULONG)password.size(),
                                        (PUCHAR)salt.data(), (ULONG)salt.size(), iterations,
                                        out.data(), (ULONG)out.size(), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) throw std::runtime_error("crypto");
#else
    if (!PKCS5_PBKDF2_HMAC(password.data(), (int)password.size(), (const unsigned char*)salt.data(),
                           (int)salt.size(), (int)iterations, EVP_sha256(), 32, out.data()))
        throw std::runtime_error("crypto");
#endif
    return hex(out.data(), out.size());
}
}  // namespace

std::string random_token() {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), (ULONG)bytes.size(),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("crypto");
#else
    if (RAND_bytes(bytes.data(), (int)bytes.size()) != 1) throw std::runtime_error("crypto");
#endif
    return hex(bytes.data(), bytes.size());
}

std::string sha256(const std::string& value) {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        throw std::runtime_error("crypto");
    auto status = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (status == 0) status = BCryptHashData(hash, (PUCHAR)value.data(), (ULONG)value.size(), 0);
    if (status == 0) status = BCryptFinishHash(hash, bytes.data(), (ULONG)bytes.size(), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) throw std::runtime_error("crypto");
#else
    unsigned int n = 0;
    if (EVP_Digest(value.data(), value.size(), bytes.data(), &n, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("crypto");
#endif
    return hex(bytes.data(), bytes.size());
}

bool secure_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;

    // 相同长度下遍历全部字节，不在第一个不匹配字节处提前返回。
    unsigned int result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) result |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return result == 0;
}

std::string hash_password(const std::string& password) {
    if (password.empty() || password.size() > 1024) throw std::invalid_argument("password length");

    // 每次生成独立随机盐；编码保存算法及成本，验证时不接受低于约定的迭代数。
    auto salt = random_token();
    return "pbkdf2_sha256$210000$" + salt + "$" + derive(password, salt, 210000);
}

bool verify_password(const std::string& password, const std::string& encoded) {
    try {
        if (password.empty() || password.size() > 1024) return false;
        std::istringstream input(encoded);
        std::string alg, count, salt, digest, extra;
        if (!std::getline(input, alg, '$') || !std::getline(input, count, '$') ||
            !std::getline(input, salt, '$') || !std::getline(input, digest, '$') ||
            std::getline(input, extra, '$'))
            return false;
        if (alg != "pbkdf2_sha256" || salt.size() != 64 || digest.size() != 64 || count.empty() ||
            count.find_first_not_of("0123456789") != std::string::npos)
            return false;

        // 限制配置中的计算成本，防止损坏哈希触发无界 CPU 消耗。
        auto iterations = std::stoul(count);
        if (iterations < 210000 || iterations > 1000000) return false;
        return secure_equal(digest, derive(password, salt, iterations));
    } catch (...) {
        return false;
    }
}
}  // namespace ems::web
