#pragma once
#include <string>
#include "RakNet/Source/RakNetTypes.h"
namespace VitaAvatar { struct VerifiedOutfit; }
namespace RBX { namespace Network {
class Player; class Server; class ServerReplicator;
std::string makeOfflineAvatarWire(const VitaAvatar::VerifiedOutfit& outfit);
bool submitOfflineAvatar(Player* player,const std::string& payload);
bool acceptOfflineAvatar(Player* player,const std::string& payload);
bool receiveOfflineAvatarAssets(const std::string& payload);
void sendOfflineAvatarAssets(Server* server,const RakNet::SystemAddress& target);
void releaseOfflineAvatar(Player* player);
bool restoreOfflineAvatar(Player* player);
bool deferOfflineAvatar(const ServerReplicator* connection,const std::string& payload);
void applyDeferredOfflineAvatar(const ServerReplicator* connection,Player* player);
void releaseDeferredOfflineAvatar(const ServerReplicator* connection);
} }
