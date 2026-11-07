#pragma once
#include "LoaderDetailsNetwork.h"
#include "util/VitaBadgeStore.h"
#include <ctime>

namespace VitaBrowser {
struct BadgeMedia {
    int id = 0;
    std::string name, description, game, iconPath, json, error;
    std::shared_ptr<DetailsPublication> publication;
};
inline std::string badgeCache(int id, const char* extension) {
    return "ux0:/data/Bloxicon/system/badges/" + std::to_string(id) + extension;
}
inline bool parseBadgeMetadata(const std::string& json, int id, BadgeMedia& result) {
    rapidjson::Document document; std::string error;
    if (!parsePublicMetadataJson(json, document, error) || !document.HasMember("id") ||
        !document["id"].IsInt64() || document["id"].GetInt64() != id ||
        !document.HasMember("name") || !document["name"].IsString() ||
        !document.HasMember("description") || !document["description"].IsString()) return false;
    BadgeMedia value; value.id = id; value.json = json;
    value.name = RBX::VitaBadges::clean(std::string(document["name"].GetString(), document["name"].GetStringLength()), 160);
    value.description = RBX::VitaBadges::clean(std::string(document["description"].GetString(), document["description"].GetStringLength()), 1200);
    if (document.HasMember("awardingUniverse") && document["awardingUniverse"].IsObject()) {
        const auto& game = document["awardingUniverse"];
        if (game.HasMember("name") && game["name"].IsString())
            value.game = RBX::VitaBadges::clean(std::string(game["name"].GetString(), game["name"].GetStringLength()));
    }
    if (value.name.empty()) return false;
    result = std::move(value); return true;
}
inline BadgeMedia cachedBadge(int id) {
    BadgeMedia result; result.id = id;
    const std::string path = badgeCache(id, ".json");
    if (fileSize(path) <= 64 * 1024) {
        std::ifstream input(path, std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        parseBadgeMetadata(json, id, result);
    }
    return result;
}
inline BadgeMedia requestBadge(const BadgeMedia& cached,const std::atomic<bool>& cancelled){(void)cancelled;auto result=cached;result.iconPath.clear();return result;}
inline BadgeMedia publishBadge(BadgeMedia result){result.iconPath.clear();return result;}
inline std::string badgeDate(long long timestamp) {
    const std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm* value = std::gmtime(&time); char text[40] = {};
    if (!value || !std::strftime(text, sizeof(text), "%d %b %Y", value)) return "Date unavailable";
    return text;
}
}
