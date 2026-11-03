#pragma once
#include "AvatarProfile.h"

namespace VitaAvatar {
// One replica per authenticated account and service origin. A pending local look
// is retained until acknowledged; neither polling nor a stale write can erase it.
struct AccountReplica {
    enum Decision { Pull, Push, Conflict };
    uint64_t version=0;
    std::string base,local;
    bool dirty()const{return !base.empty()&&local!=base;}
    static std::string canonical(const std::string& bytes){
        Profile value;std::string error;
        if(parseProfile(bytes,value,error)!=ProfileRead::Valid)throw std::runtime_error("The account avatar profile is invalid; local outfit was kept.");
        return encodeProfile(value);
    }
    Decision decide(uint64_t remoteVersion,const std::string& remote)const{
        if(!version||!dirty()||local==remote)return Pull;
        return version==remoteVersion&&base==remote?Push:Conflict;
    }
    void accept(uint64_t remoteVersion,const std::string& remote){version=remoteVersion;base=local=canonical(remote);}
    // Preserve edits made after the submitted snapshot left the device.
    void acknowledge(uint64_t remoteVersion,const std::string& submitted,const std::string& remote){
        auto latest=local;accept(remoteVersion,remote);if(latest!=submitted)local=latest;
    }
};
}
