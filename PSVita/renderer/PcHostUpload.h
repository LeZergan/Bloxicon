#pragma once
#include "VitaPlaceTransfer.h"
#include "network/VitaTcpTunnel.h"
#include <array>
#include <fstream>
#include <algorithm>

namespace VitaAdhoc {
// Prepared client path: send the selected original as-is on the existing
// UDP/TCP transfer transport. No keyboard mode or place rewriting is involved.
class PcHostUpload {
public:
    ~PcHostUpload() { close(); }
    bool begin(const SceNetSockaddrIn& host, const std::string& path,
               uint64_t requestNonce, uint64_t fingerprint, std::string& error) {
        close(); nonce=requestNonce;hash=fingerprint;
        source.open(path.c_str(),std::ios::binary|std::ios::ate);
        if (!source || source.tellg()<=0 || uint64_t(source.tellg())>Transfer::maxFileSize) {
            error="The original game cannot be sent to this server.";close();return false;
        }
        total=uint64_t(source.tellg());source.seekg(0);
        destination=host;destination.sin_port=sceNetHtons(Transfer::port);
        socket=sceNetSocket("Original game upload",SCE_NET_AF_INET,SCE_NET_SOCK_DGRAM,SCE_NET_IPPROTO_UDP);
        const int enabled=1;
        if(socket<0 || sceNetSetsockopt(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,&enabled,sizeof(enabled))<0) {
            error="Could not open original game transfer.";close();return false;
        }
        silenceDeadline=sceKernelGetSystemTimeWide()+30000000ULL;
        return true;
    }
    bool poll(std::string& error) {
        if(socket<0)return false;
        if(acknowledged==total)return true;
        const uint64_t now=sceKernelGetSystemTimeWide();
        if(now>=silenceDeadline) {error="Original game transfer timed out. Refresh and try again.";return false;}
        // The confirmed stream can carry the existing complete bounded window
        // per poll. Keep the conservative burst for actual UDP radio traffic.
        const unsigned sendBudget=RBX::Network::vitaTcpTunnelEnabled(destination.sin_addr.s_addr)?unsigned(slots.size()):64u;
        for(unsigned i=0;i<256;++i) {
            std::array<unsigned char,41> reply{};SceNetSockaddrIn sender{};unsigned length=sizeof(sender);
            const int size=RBX::Network::vitaRecvfrom(socket,reply.data(),reply.size(),SCE_NET_MSG_DONTWAIT,
                reinterpret_cast<SceNetSockaddr*>(&sender),&length);
            if(size<=0)break;
            if(size!=40 || sender.sin_addr.s_addr!=destination.sin_addr.s_addr || sender.sin_port!=destination.sin_port ||
                Transfer::get(reply.data(),0,4)!=0x52425855 || Transfer::get(reply.data(),4,2)!=1 ||
                Transfer::get(reply.data(),6,2)!=3 || Transfer::get(reply.data(),8,8)!=nonce ||
                Transfer::get(reply.data(),16,8)!=hash || Transfer::get(reply.data(),32,8)!=total)continue;
            const uint64_t offset=Transfer::get(reply.data(),24,8);
            if(offset%Transfer::chunkSize)continue;
            auto& slot=slots[(offset/Transfer::chunkSize)%slots.size()];
            if(slot.active && slot.offset==offset) {
                acknowledged+=std::min(uint64_t(Transfer::chunkSize),total-offset);
                slot.active=false;silenceDeadline=now+30000000ULL;
            }
        }
        for(unsigned filled=0;filled<sendBudget && nextOffset<total;++filled) {
            auto& slot=slots[(nextOffset/Transfer::chunkSize)%slots.size()];
            if(slot.active)break;
                slot.active=true;slot.offset=nextOffset;slot.sentAt=0;
                slot.count=std::min(uint64_t(Transfer::chunkSize),total-nextOffset);
                source.read(reinterpret_cast<char*>(slot.bytes.data()),slot.count);
                if(size_t(source.gcount())!=slot.count) {error="Could not read the original game.";return false;}
                nextOffset+=Transfer::chunkSize;
        }
        unsigned sent=0;
        for(auto& slot:slots) {
            if(!slot.active || (slot.sentAt && now-slot.sentAt<500000ULL) || sent>=sendBudget)continue;
            const size_t count=std::min(uint64_t(Transfer::chunkSize),total-slot.offset);
            std::array<unsigned char,40+Transfer::chunkSize> packet{};
            Transfer::put(packet,0,0x52425855,4);Transfer::put(packet,4,1,2);Transfer::put(packet,6,1,2);
            Transfer::put(packet,8,nonce,8);Transfer::put(packet,16,hash,8);
            Transfer::put(packet,24,slot.offset,8);Transfer::put(packet,32,total,8);
            std::copy_n(slot.bytes.data(),count,packet.data()+40);
            const int written=RBX::Network::vitaSendto(socket,packet.data(),unsigned(40+count),0,
                reinterpret_cast<const SceNetSockaddr*>(&destination),sizeof(destination));
            if(written==int(40+count))slot.sentAt=now;
            ++sent;
        }
        return true;
    }
    unsigned percent() const {return total?unsigned(acknowledged*100/total):0;}
    void close() {
        if(socket>=0)RBX::Network::vitaSocketClose(socket);
        socket=-1;source.close();source.clear();
        for(auto& slot:slots) {slot.active=false;slot.offset=slot.sentAt=slot.count=0;}
        total=nextOffset=acknowledged=0;
    }
private:
    struct Slot {uint64_t offset=0,sentAt=0;size_t count=0;bool active=false;std::array<unsigned char,Transfer::chunkSize> bytes{};};
    std::array<Slot,256> slots{};
    std::ifstream source;
    int socket=-1;
    SceNetSockaddrIn destination{};
    uint64_t nonce=0,hash=0,total=0,nextOffset=0,acknowledged=0,silenceDeadline=0;
};
}
