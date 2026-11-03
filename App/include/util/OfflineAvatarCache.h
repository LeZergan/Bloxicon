#pragma once
#include <string>
#include <boost/filesystem.hpp>
namespace RBX { namespace OfflineAvatarCache {
inline bool validHash(const std::string& hash) { return hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos; }
inline boost::filesystem::path directory() {
#ifdef RBX_PLATFORM_PSVITA
    return boost::filesystem::path("ux0:/data/RBXV00001/system/avatar/network");
#else
    return boost::filesystem::temp_directory_path()/"rbxv-avatars";
#endif
}
inline std::string path(const std::string& hash) { return validHash(hash) ? (directory()/hash).string() : std::string(); }
inline bool isAssetUri(const std::string& uri) {
    const std::string prefix="rbxasset://avatar-network/";
    return uri.compare(0,prefix.size(),prefix)==0 && validHash(uri.substr(prefix.size()));
}
inline std::string resolve(const std::string& uri) {
    const std::string prefix="rbxasset://avatar-network/";
    if(uri.compare(0,prefix.size(),prefix)) return {};
    const std::string file=path(uri.substr(prefix.size()));
    return !file.empty() && boost::filesystem::is_regular_file(file) ? file : std::string();
}
} }
