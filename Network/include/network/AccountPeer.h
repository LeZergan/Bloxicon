#pragma once
// Private gateway/relay admission for the source-built dedicated engine.
// No client-supplied token, player property or network address grants identity.
#include <array>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/hmac.h>
#endif

namespace RBX { namespace Network { namespace AccountPeer {
struct Identity { int userId = 0; std::string username, nonce; unsigned roleFlags = 0; };

inline bool hex(const std::string& value, size_t count) {
    if (value.size() != count) return false;
    for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool name(const std::string& value) {
    if (value.size() < 3 || value.size() > 20) return false;
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!letter(value[0])) return false;
    for (char c : value) if (!letter(c) && !(c >= '0' && c <= '9') && c != '_') return false;
    return true;
}
inline long long number(const std::string& value) {
    if (value.empty() || value.size() > 13 || value[0] == '0') return -1;
    long long result = 0;
    for (char c : value) { if (c < '0' || c > '9') return -1; result = result * 10 + c - '0'; }
    return result;
}
inline std::string readPrivate(const std::string& path, size_t maximum) {
#ifdef _WIN32
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Account peer file unavailable");
    std::string data(maximum + 1, '\0');
    in.read(&data[0], data.size()); data.resize(size_t(in.gcount()));
#else
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) throw std::runtime_error("Account peer file unavailable");
    struct Owner { int fd; ~Owner() { close(fd); } } owner{fd};
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || (info.st_mode & 0077))
        throw std::runtime_error("Account peer file must be private and regular");
    std::string data(maximum + 1, '\0');
    size_t count = 0;
    while (count < data.size()) {
        const auto n = ::read(fd, &data[count], data.size() - count);
        if (n < 0) throw std::runtime_error("Account peer file unreadable");
        if (!n) break;
        count += size_t(n);
    }
    data.resize(count);
#endif
    if (data.size() > maximum) throw std::runtime_error("Account peer file too large");
    return data;
}
inline std::string signature(const std::string& key, const std::string& body) {
    unsigned char digest[32];
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    struct Owner {
        BCRYPT_ALG_HANDLE& algorithm; BCRYPT_HASH_HANDLE& hash;
        ~Owner() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } owner{algorithm, hash};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, (PUCHAR)key.data(), ULONG(key.size()), 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)body.data(), ULONG(body.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
        throw std::runtime_error("Account peer signature unavailable");
#else
    unsigned size = 0;
    if (!HMAC(EVP_sha256(), key.data(), int(key.size()), (const unsigned char*)body.data(), body.size(), digest, &size) || size != 32)
        throw std::runtime_error("Account peer signature unavailable");
#endif
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : digest) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}

struct Config {
    std::string directory, instance, key;
    bool enabled() const { return !directory.empty(); }
    bool verify(const std::string& data, int gamePort, int peerPort, Identity& identity,
                long long now = static_cast<long long>(std::time(nullptr))) const {
        if (!enabled() || key.size() != 32 || !hex(instance, 32) || data.empty() || data.size() > 256) return false;
        std::vector<std::string> fields;
        size_t start = 0;
        for (size_t i = 0; i < data.size(); ++i) {
            const unsigned char c = data[i];
            if (c < 32 || c > 126) return false;
            if (c == ';') { fields.push_back(data.substr(start, i - start)); start = i + 1; }
        }
        fields.push_back(data.substr(start));
        if (fields.size() != 10 || fields[0] != "RBXP2" || fields[1] != instance || !hex(fields[9], 64) ||
            number(fields[3]) != gamePort || number(fields[4]) != peerPort ||
            number(fields[2]) <= now || number(fields[2]) - now > 15 || !name(fields[6]) || !hex(fields[7], 32) ||
            fields[5].size() != 11 || fields[5][0] != '-') return false;
        const auto positive = number(fields[5].substr(1));
        if (positive < 2000000001LL || positive > 2147483647LL) return false;
        const auto roles = fields[8] == "0" ? 0 : number(fields[8]);
        if (roles < 0 || roles > 15) return false;
        const auto expected = signature(key, data.substr(0, start - 1));
        unsigned difference = 0;
        for (size_t i = 0; i < expected.size(); ++i) difference |= unsigned(expected[i] ^ fields[9][i]);
        if (difference) return false;
        identity = Identity{-int(positive), fields[6], fields[7], unsigned(roles)};
        return true;
    }
    bool read(int gamePort, int peerPort, Identity& identity) const {
        try { return verify(readPrivate(directory + "/" + std::to_string(peerPort) + ".lease", 256), gamePort, peerPort, identity); }
        catch (...) { return false; }
    }
};
inline const Config& config() {
    static const Config value = [] {
        Config result;
        const char* directory = std::getenv("RBXV_ACCOUNT_PEER_DIRECTORY");
        const char* instance = std::getenv("RBXV_ACCOUNT_PEER_INSTANCE");
        const char* key = std::getenv("RBXV_ACCOUNT_PEER_KEY_FILE");
        if (!directory && !instance) return result; // Ordinary local-network hosting.
        if (!directory || !instance || !key || !*directory || !hex(instance, 32))
            throw std::runtime_error("Incomplete account peer configuration");
        result.directory = directory; result.instance = instance; result.key = readPrivate(key, 32);
        if (result.key.size() != 32) throw std::runtime_error("Invalid account peer key");
        return result;
    }();
    return value;
}
} } }
