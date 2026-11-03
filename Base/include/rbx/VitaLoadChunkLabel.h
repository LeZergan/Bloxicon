#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RBX {
// Diagnostic peek only: never consume or change the deserializer's stream.
inline void vitaLoadChunkLabel(char* out, size_t capacity, const char* data, size_t size)
{
    if (!capacity) return;
    out[0] = 0;
    if (!data || size < 8) return;
    uint32_t length = 0;
    std::memcpy(&length, data + 4, sizeof(length));
    if (length > size - 8) return;
    size_t count = length < capacity - 1 ? length : capacity - 1;
    for (size_t i = 0; i < count; ++i) {
        unsigned char c = static_cast<unsigned char>(data[8 + i]);
        out[i] = c >= 32 && c < 127 ? c : '?';
    }
    out[count] = 0;
}
}
