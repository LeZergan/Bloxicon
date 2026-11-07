#pragma once
#include <array>
#include <strings.h>

namespace VitaOfficialEndpoint {
inline constexpr char host[]="";
// Optional operator-provided DNS fallback. Disabled in this source export.
inline constexpr std::array<unsigned char,4> ipv4{};
inline bool matches(const char* name) {return name&&host[0]&&strcasecmp(name,host)==0;}
}
