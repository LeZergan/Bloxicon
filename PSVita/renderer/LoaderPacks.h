#pragma once
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <string>
#include <vector>
namespace VitaBrowser {
struct PackResult {std::string path;bool loaded;};
inline std::vector<PackResult>& packResults(){static std::vector<PackResult> value;return value;}
}
