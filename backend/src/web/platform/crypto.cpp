#include "crypto.h"

#include <argon2.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "web/transport/json.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#ifdef _WIN32
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif
namespace ems::web {
namespace {
std::atomic<int> deriving{0};

struct PasswordSlot {
    PasswordSlot() {
        if (deriving.fetch_add(1) >= 2) {
            deriving.fetch_sub(1);
            throw ApiError(429, "ResourceExhausted", "rate_limited");
        }
    }

    ~PasswordSlot() { deriving.fetch_sub(1); }
};

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

    PasswordSlot slot;
    auto salt = digest_bytes(random_token());
    std::array<char, 256> encoded{};
    if (argon2id_hash_encoded(3, 65536, 1, password.data(), password.size(), salt.data(), 16, 32,
                              encoded.data(), encoded.size()) != ARGON2_OK)
        throw ApiError(503, "Unavailable", "password_derivation_unavailable");
    return encoded.data();
}

bool verify_password(const std::string& password, const std::string& encoded) {
    PasswordSlot slot;
    try {
        if (encoded.rfind("$argon2id$", 0) == 0) {
            unsigned m = 0, t = 0, p = 0;
            int end = 0;
            if (std::sscanf(encoded.c_str(), "$argon2id$v=19$m=%u,t=%u,p=%u$%n", &m, &t, &p,
                            &end) != 3 ||
                end == 0 || m < 19456 || m > 65536 || t < 2 || t > 4 || p != 1 ||
                encoded.size() > 256 || password.empty() || password.size() > 1024)
                return false;
            const auto status = argon2id_verify(encoded.c_str(), password.data(), password.size());
            if (status == ARGON2_MEMORY_ALLOCATION_ERROR || status == ARGON2_THREAD_FAIL)
                throw ApiError(503, "Unavailable", "password_derivation_unavailable");
            return status == ARGON2_OK;
        }
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
    } catch (const ApiError&) {
        throw;
    } catch (...) {
        return false;
    }
}

std::vector<char> digest_bytes(const std::string& value) {
    if (value.size() % 2) throw std::invalid_argument("hex");
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::invalid_argument("hex");
    };
    std::vector<char> bytes;
    for (std::size_t i = 0; i < value.size(); i += 2)
        bytes.push_back(char((nibble(value[i]) << 4) | nibble(value[i + 1])));
    return bytes;
}

std::string random_csrf() {
    const auto bytes = digest_bytes(random_token());
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    unsigned bits = 0;
    int count = 0;
    std::string out;
    for (unsigned char c : bytes) {
        bits = (bits << 8) | c;
        count += 8;
        while (count >= 6) {
            count -= 6;
            out += alphabet[(bits >> count) & 63];
        }
    }
    if (count) out += alphabet[(bits << (6 - count)) & 63];
    return out;
}

std::string hmac_sha256(const std::string& key, const std::string& value) {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        throw std::runtime_error("crypto");
    auto status =
        BCryptCreateHash(alg, &hash, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0);
    if (status == 0) status = BCryptHashData(hash, (PUCHAR)value.data(), (ULONG)value.size(), 0);
    if (status == 0) status = BCryptFinishHash(hash, bytes.data(), (ULONG)bytes.size(), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) throw std::runtime_error("crypto");
#else
    unsigned int size = 0;
    if (!HMAC(EVP_sha256(), key.data(), (int)key.size(), (const unsigned char*)value.data(),
              value.size(), bytes.data(), &size))
        throw std::runtime_error("crypto");
#endif
    return hex(bytes.data(), bytes.size());
}
}  // namespace ems::web
