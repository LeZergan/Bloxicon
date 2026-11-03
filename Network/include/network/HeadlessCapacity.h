#pragma once

#include <cstdlib>

namespace RBX { namespace Network {

inline int headlessMaxPlayers()
{
    const char* value = std::getenv("RBXV_MAX_PLAYERS");
    if (!value || !*value) return 10;
    char* end = nullptr;
    const long count = std::strtol(value, &end, 10);
    return end != value && *end == '\0' && count >= 1 && count <= 10 ? static_cast<int>(count) : 10;
}

}}
