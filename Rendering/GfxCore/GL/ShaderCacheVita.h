// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <openssl/sha.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Cache only binaries produced by this exact vitaGL library and shader source.
// No map data is duplicated, and a missing/incomplete entry recompiles normally.
namespace {
std::map<unsigned, std::string> vitaShaderKeys;

std::string vitaDigest(const void* bytes, size_t size)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(static_cast<const unsigned char*>(bytes), size, digest);
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char b: digest) { result += hex[b >> 4]; result += hex[b & 15]; }
    return result;
}

std::string vitaProgramCachePath(unsigned vertex, unsigned fragment)
{
    const std::string key = std::string(RBX_VITAGL_CACHE_ABI) + ":v1:" +
        vitaShaderKeys.at(vertex) + ":" + vitaShaderKeys.at(fragment);
    sceIoMkdir("ux0:/data/Bloxicon", 0777);
    sceIoMkdir("ux0:/data/Bloxicon/system/programs", 0777);
    return "ux0:/data/Bloxicon/system/programs/" + vitaDigest(key.data(), key.size()) + ".bin";
}

bool vitaLoadProgram(unsigned program, const std::string& path)
{
    // These generated files are outside the runtime pack. Native descriptors
    // avoid newlib's unchecked descriptor lookup in ftell, seen in a Vita dump.
    const int fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    struct CacheFile {
        int fd;
        ~CacheFile() { sceIoClose(fd); }
    } file{fd};
    const int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
    if (size <= 64 || size > 8 * 1024 * 1024 || sceIoLseek32(fd, 0, SCE_SEEK_SET) != 0) return false;
    char checksum[64];
    std::vector<unsigned char> binary(static_cast<size_t>(size) - 64);
    const bool read = sceIoRead(fd, checksum, sizeof(checksum)) == sizeof(checksum) &&
        sceIoRead(fd, binary.data(), binary.size()) == static_cast<int>(binary.size());
    if (!read || vitaDigest(binary.data(), binary.size()) != std::string(checksum, 64)) return false;
    glProgramBinary(program, 0, binary.data(), static_cast<GLsizei>(binary.size()));
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    return linked != 0;
}

void vitaSaveProgram(unsigned program, const std::string& path)
{
    GLint size = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &size);
    if (size <= 0 || size > 8 * 1024 * 1024) return;
    std::vector<unsigned char> binary(size);
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(program, size, &written, &format, binary.data());
    if (written <= 0 || written > size) return;
    const std::string checksum = vitaDigest(binary.data(), written);
    const std::string temporary = path + ".tmp";
    FILE* f = std::fopen(temporary.c_str(), "wb");
    if (!f) return;
    const bool saved = std::fwrite(checksum.data(), 1, checksum.size(), f) == checksum.size() &&
        std::fwrite(binary.data(), 1, written, f) == static_cast<size_t>(written);
    const int closed = std::fclose(f);
    if (saved && closed == 0)
    {
        sceIoRename(temporary.c_str(), path.c_str());
    }
}
}
