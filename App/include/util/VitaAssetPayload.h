#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <zlib.h>

namespace RBX {
namespace VitaAssetPayload {

inline bool isGzip(const std::string& bytes)
{
    return bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0x1f &&
        static_cast<unsigned char>(bytes[1]) == 0x8b;
}

// Side-loaded assets can contain the HTTP gzip representation of an otherwise
// ordinary PNG/model/sound. Decode before dispatch so each existing decoder
// sees the original asset bytes. Keep untrusted expansion bounded on Vita.
inline bool decodeGzip(const std::string& input, std::string* output,
    size_t maxOutputBytes = 16u * 1024u * 1024u)
{
    if (!output || !isGzip(input) || input.size() < 18 ||
        input.size() > 16u * 1024u * 1024u)
        return false;

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
        return false;

    struct InflaterOwner
    {
        z_stream* value;
        ~InflaterOwner() { inflateEnd(value); }
    } owner = { &stream };

    const unsigned char* tail = reinterpret_cast<const unsigned char*>(input.data() + input.size() - 4);
    const size_t declaredSize = size_t(tail[0]) | (size_t(tail[1]) << 8) |
        (size_t(tail[2]) << 16) | (size_t(tail[3]) << 24);
    if (declaredSize > maxOutputBytes) return false;

    // Inflate into one bounded final buffer. zlib validates both CRC and ISIZE;
    // the trailer is an allocation hint, never proof that the payload is valid.
    // The extra byte detects a false size without allocating/growing a buffer.
    std::string decoded(declaredSize, '\0');
    unsigned char overflow = 0;
    stream.next_out = declaredSize ? reinterpret_cast<Bytef*>(&decoded[0]) : &overflow;
    stream.avail_out = static_cast<uInt>(declaredSize ? declaredSize : 1);
    int status = Z_OK;
    do
    {
        const uLong beforeIn = stream.total_in, beforeOut = stream.total_out;
        status = inflate(&stream, Z_NO_FLUSH);
        if (stream.total_out > declaredSize) return false;
        if (status == Z_OK && stream.total_in == beforeIn && stream.total_out == beforeOut)
            return false;
        if (!stream.avail_out) { stream.next_out = &overflow; stream.avail_out = 1; }
    }
    while (status == Z_OK);

    const bool valid = status == Z_STREAM_END && stream.avail_in == 0 && stream.total_out == declaredSize;
    if (!valid)
        return false;
    output->swap(decoded);
    return true;
}

} // namespace VitaAssetPayload
} // namespace RBX
