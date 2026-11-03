#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace RBX {
struct VitaAvatarThumbnail
{
    std::string username, userId, size;
    bool headshot = false;
};
inline bool vitaLegacyAvatarThumbnail(const std::string& url, VitaAvatarThumbnail& output)
{
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){return std::tolower(c);});
    const char* origins[] = {"http://www.roblox.com/", "https://www.roblox.com/", "http://roblox.com/", "https://roblox.com/"};
    size_t path = std::string::npos;
    for (const char* origin : origins)
        if (lower.find(origin) == 0) {path = std::char_traits<char>::length(origin); break;}
    if (path == std::string::npos) return false;
    const size_t query = lower.find('?', path);
    if (query == std::string::npos) return false;
    const std::string endpoint = lower.substr(path, query-path);
    VitaAvatarThumbnail parsed;
    if (endpoint == "thumbs/avatarheadshot.ashx") parsed.headshot = true;
    else if (endpoint != "thumbs/avatar.ashx") return false;
    unsigned width = 0, height = 0;
    for (size_t cursor = query+1; cursor < lower.size(); )
    {
        const size_t end = lower.find('&', cursor);
        const std::string field = lower.substr(cursor, end == std::string::npos ? end : end-cursor);
        const size_t equals = field.find('=');
        if (equals != std::string::npos)
        {
            const std::string key = field.substr(0, equals), value = field.substr(equals+1);
            const bool number = !value.empty() && value.size() <= 16 && value.find_first_not_of("0123456789") == std::string::npos;
            if (key == "username")
            {
                if (value.empty() || value.size()>20 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos) return false;
                parsed.username = value;
            }
            else if (key == "userid")
            {
                if (!number || value.find_first_not_of('0') == std::string::npos) return false;
                parsed.userId = value;
            }
            else if (number && value.size() <= 4)
            {
                unsigned n=0;for (char c:value) n=n*10+(c-'0');
                if (key=="x" || key=="width") width=n;
                else if (key=="y" || key=="height") height=n;
            }
        }
        if (end == std::string::npos) break;
        cursor = end+1;
    }
    if (parsed.username.empty() && parsed.userId.empty()) return false;
    parsed.size = width && height && width<=150 && height<=150 ? "150x150" : "420x420";
    output = std::move(parsed);
    return true;
}
}
