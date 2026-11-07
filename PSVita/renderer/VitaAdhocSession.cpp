#include "VitaAdhocSession.h"
#include "VitaTransferWindow.h"
#include "PcHostProtocol.h"
#include "VitaPlaceTransfer.h"
#include "ServerPresentation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <new>
#include <set>

namespace VitaAdhoc
{
namespace
{
static const size_t kNameBytes = 24;
static const size_t kPlaceBytes = 80;
static const size_t kWireBytes = 4 + 2 + 2 + 8 + 8 + kNameBytes + kPlaceBytes;
static const size_t kLanControlWireBytes = 4 + 2 + 1 + 1 + 2 + 8 + 8;

void write16(std::vector<unsigned char>& out, size_t offset, uint16_t value)
{
    out[offset] = static_cast<unsigned char>(value >> 8);
    out[offset + 1] = static_cast<unsigned char>(value);
}

void write32(std::vector<unsigned char>& out, size_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[offset + i] = static_cast<unsigned char>(value >> (24 - i * 8));
}

void write64(std::vector<unsigned char>& out, size_t offset, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        out[offset + i] = static_cast<unsigned char>(value >> (56 - i * 8));
}

uint16_t read16(const unsigned char* bytes, size_t offset)
{
    return uint16_t(bytes[offset] << 8) | bytes[offset + 1];
}

uint32_t read32(const unsigned char* bytes, size_t offset)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result = (result << 8) | bytes[offset + i];
    return result;
}

uint64_t read64(const unsigned char* bytes, size_t offset)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
        result = (result << 8) | bytes[offset + i];
    return result;
}

void writeString(std::vector<unsigned char>& out, size_t offset, size_t capacity, const std::string& value)
{
    size_t count = std::min(capacity - 1, value.size());
    while (count < value.size() && count && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80) --count;
    std::copy(value.begin(), value.begin() + count, out.begin() + offset);
}

std::string readString(const unsigned char* bytes, size_t offset, size_t capacity)
{
    size_t count = 0;
    while (count < capacity && bytes[offset + count])
        ++count;
    return std::string(reinterpret_cast<const char*>(bytes + offset), count);
}
}

std::vector<unsigned char> encodeAdvertisement(const Advertisement& value)
{
    std::vector<unsigned char> out(kWireBytes, 0);
    write32(out, 0, value.onDemand ? kPcServiceMagic : kAdvertisementMagic);
    write16(out, 4, value.version);
    write16(out, 6, value.serverPort);
    write64(out, 8, value.placeHash);
    write64(out, 16, value.placeId);
    writeString(out, 24, kNameBytes, value.hostName);
    writeString(out, 24 + kNameBytes, kPlaceBytes, value.placeName);
    if (value.rosterKnown) {
        const size_t count = std::min(size_t(8), value.playerNames.size());
        out.resize(kWireBytes + 8 + count * kNameBytes, 0);
        write32(out, kWireBytes, 0x5242584du); // RBXM roster extension
        write16(out, kWireBytes + 4, 1);
        write16(out, kWireBytes + 6, uint16_t(count));
        for (size_t i=0; i<count; ++i) writeString(out,kWireBytes+8+i*kNameBytes,kNameBytes,value.playerNames[i]);
    }
    else if (value.quotaKnown) {
        out.resize(kWireBytes + 14, 0);
        write32(out, kWireBytes, 0x52425851u); // RBXQ capacity extension
        write16(out, kWireBytes + 4, 1);
        write16(out, kWireBytes + 6, 6);
        out[kWireBytes+8]=value.playersUsed;
        out[kWireBytes+9]=value.playersLimit;
        out[kWireBytes+10]=value.serversUsed;
        out[kWireBytes+11]=value.serversLimit;
        out[kWireBytes+12]=value.totalPlayers;
        out[kWireBytes+13]=value.totalCapacity;
    }
    return out;
}

bool decodeAdvertisement(const void* data, size_t size, Advertisement& value)
{
    if (!data || size < kWireBytes || size > kWireBytes + 8 + 8*kNameBytes)
        return false;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    const uint32_t magic = read32(bytes, 0);
    if (magic != kAdvertisementMagic && magic != kPcServiceMagic)
        return false;
    const uint16_t version = read16(bytes, 4);
    const uint16_t port = read16(bytes, 6);
    if (version != kAdvertisementVersion || port == 0)
        return false;

    Advertisement parsed;
    parsed.onDemand = magic == kPcServiceMagic;
    parsed.version = version;
    parsed.serverPort = port;
    parsed.placeHash = read64(bytes, 8);
    parsed.placeId = read64(bytes, 16);
    parsed.hostName = readString(bytes, 24, kNameBytes);
    parsed.placeName = readString(bytes, 24 + kNameBytes, kPlaceBytes);
    if (parsed.hostName.empty() || parsed.placeName.empty())
        return false;
    if (size != kWireBytes) {
        if (size < kWireBytes+8 || read16(bytes,kWireBytes+4)!=1) return false;
        const uint32_t extension=read32(bytes,kWireBytes);
        if (extension==0x52425851u) {
            if(size!=kWireBytes+14 || read16(bytes,kWireBytes+6)!=6) return false;
            parsed.quotaKnown=true;
            parsed.playersUsed=bytes[kWireBytes+8]; parsed.playersLimit=bytes[kWireBytes+9];
            parsed.serversUsed=bytes[kWireBytes+10]; parsed.serversLimit=bytes[kWireBytes+11];
            parsed.totalPlayers=bytes[kWireBytes+12]; parsed.totalCapacity=bytes[kWireBytes+13];
            if(!parsed.playersLimit || !parsed.serversLimit ||
                parsed.playersUsed>parsed.playersLimit || parsed.serversUsed>parsed.serversLimit ||
                parsed.totalPlayers>parsed.totalCapacity) return false;
        } else if (extension==0x5242584du) {
            const size_t count=read16(bytes,kWireBytes+6);
            if(count>8 || size!=kWireBytes+8+count*kNameBytes) return false;
            parsed.rosterKnown=true;
            for(size_t i=0;i<count;++i) {
                const size_t offset=kWireBytes+8+i*kNameBytes;
                const std::string name=readString(bytes,offset,kNameBytes);
                if(name.empty() || name.size()>=kNameBytes) return false;
                for(unsigned char c:name) if(c<32 || c==127) return false;
                parsed.playerNames.push_back(name);
            }
        } else return false;
    }
    value = parsed;
    return true;
}

std::vector<unsigned char> encodeLanControl(const LanControl& value)
{
    std::vector<unsigned char> out(kLanControlWireBytes, 0);
    write32(out, 0, kLanControlMagic);
    write16(out, 4, kAdvertisementVersion);
    out[6] = value.type;
    out[7] = value.features;
    write16(out, 8, value.serverPort);
    write64(out, 10, value.nonce);
    write64(out, 18, value.placeHash);
    return out;
}

bool decodeLanControl(const void* data, size_t size, LanControl& value)
{
    if (!data || size != kLanControlWireBytes)
        return false;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    if (read32(bytes, 0) != kLanControlMagic ||
        read16(bytes, 4) != kAdvertisementVersion ||
        (bytes[6] != LanProbe && bytes[6] != LanAck && bytes[6] != LanSnapshotEnd) || (bytes[7]&~1u))
        return false;

    LanControl parsed;
    parsed.type = bytes[6];
    parsed.features = bytes[7];
    parsed.serverPort = read16(bytes, 8);
    parsed.nonce = read64(bytes, 10);
    parsed.placeHash = read64(bytes, 18);
    if (!parsed.serverPort || !parsed.nonce)
        return false;
    value = parsed;
    return true;
}

} // namespace VitaAdhoc

#ifdef __vita__

#include <psp2/kernel/threadmgr.h>
#include "network/VitaTcpTunnel.h"
#include "network/VitaOnlineTiming.h"
#include "VitaOfficialEndpoint.h"
#include <psp2/io/dirent.h>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include "ServerEndpoint.h"
#include <sstream>
#include <psp2/net/adhoc_matching.h>
#include <psp2/net/netctl.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/sysmodule.h>
#include <cstdlib>
#include <cstdio>
#include <malloc.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace VitaAdhoc
{
namespace
{
enum MatchingEvent
{
    MatchingHello = 1,
    MatchingJoin = 2,
    MatchingLeft = 3,
    MatchingReject = 4,
    MatchingCancel = 5,
    MatchingAccept = 6,
    MatchingComplete = 7,
    MatchingTimeout = 8,
    MatchingError = 9,
    MatchingDisconnect = 10,
    MatchingData = 11,
    MatchingDataConfirm = 12,
    MatchingDataTimeout = 13
};

std::mutex callbackOwnerMutex;
std::condition_variable callbackOwnerIdle;
Session* callbackOwner = nullptr;
unsigned callbacksInFlight = 0;
bool callbackOwnerStopping = false;
const uint64_t kHostExpiryUs = 5000000;

bool sameAddress(const SceNetInAddr& a, const SceNetInAddr& b)
{
    return a.s_addr == b.s_addr;
}
}

struct Session::Impl
{
    enum Transport
    {
        TransportNone,
        TransportNativeAdhoc,
        TransportLan
    };

    mutable std::mutex mutex;
    int matchingId = -1;
    void* pool = nullptr;
    bool moduleLoaded = false;
    bool pspnetModuleLoaded = false;
    std::function<void()> connectionPump;
    bool ownsNativeConnection = false;
    bool ownsConnectionDialog = false;
    bool ownsAdhoc = false;
    bool ownsAdhocCtl = false;
    bool ownsMatching = false;
    bool matchingInitialized = false;
    bool hosting = false;
    bool hostAdvertisementPublished = false;
    bool connectionEstablished = false;
    bool requestPending = false;
    bool handshakeAccepted = false;
    SceNetInAddr connectedPeer = {};
    Advertisement localAdvertisement;
    Advertisement peerAdvertisement;
    std::vector<DiscoveredHost> hosts;
    std::map<uint32_t,ServerProbeState> publicPingMs;
    std::vector<SceNetInAddr> acceptedPeers;
    std::vector<SceNetInAddr> connectedPeers;
    std::string lastStatus = "Ad hoc idle";
    std::string terminalError;
    Transport transport = TransportNone;
    bool transportPrepared = false;
    bool lanMode = false;
    int lanSocket = -1;
    std::atomic<bool> lanRunning{false};
    std::thread lanThread;
    std::mutex resolverMutex;
    int publicResolver=-1;

    std::atomic<bool> sharing{false};
    int shareSocket=-1;
    int receiveSocket=-1;
    SceNetInAddr receiveAddress={};
    uint64_t receiveHash=0,receiveDeadline=0;
    uint16_t receiveGamePort=0;
    bool receiveAccount=false,receiveResponded=false;
    std::thread shareThread;

    // Caller holds mutex; only live advertisements can be selected. Matching
    // and LAN discovery share identity/expiry behavior instead of row indices.
    void expireHosts(uint64_t now)
    {
        hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [&](const DiscoveredHost& host) {
            return now >= host.lastSeenUs && now - host.lastSeenUs >= kHostExpiryUs;
        }), hosts.end());
    }

    void rememberHost(const SceNetInAddr& address, const Advertisement& value, uint64_t now,
                      const std::string& sourceProfile={})
    {
        expireHosts(now);
        auto found = std::find_if(hosts.begin(), hosts.end(), [&](const DiscoveredHost& host) {
            return sameAddress(host.address, address) && host.advertisement.serverPort == value.serverPort &&
                host.advertisement.onDemand == value.onDemand;
        });
        if (found != hosts.end())
        {
            Advertisement merged=value;
            const bool samePlace=merged.placeHash==found->advertisement.placeHash;
            if(samePlace&&!merged.rosterKnown){merged.rosterKnown=found->advertisement.rosterKnown;merged.playerNames=found->advertisement.playerNames;}
            if(samePlace&&!merged.quotaKnown){
                const auto& old=found->advertisement;merged.quotaKnown=old.quotaKnown;
                merged.playersUsed=old.playersUsed;merged.playersLimit=old.playersLimit;
                merged.serversUsed=old.serversUsed;merged.serversLimit=old.serversLimit;
                merged.totalPlayers=old.totalPlayers;merged.totalCapacity=old.totalCapacity;
            }
            found->advertisement = std::move(merged);
            if(!sourceProfile.empty())found->sourceProfile=sourceProfile;
            auto ping=publicPingMs.find(address.s_addr);
            if(ping!=publicPingMs.end()){found->pingMs=ping->second.milliseconds;found->pingKnown=ping->second.fresh(now);found->pingSampledUs=ping->second.sampledUs;}
            found->lastSeenUs = now;
        }
        else if (hosts.size() < 64)
        {
            std::fprintf(stderr,"Vita multiplayer discovered peer=%08x port=%u hash=%016llx service=%d\n",
                unsigned(address.s_addr),unsigned(value.serverPort),
                static_cast<unsigned long long>(value.placeHash),int(value.onDemand));
            DiscoveredHost host;
            host.address = address;
            host.advertisement = value;
            host.sourceProfile = sourceProfile;
            host.lastSeenUs = now;
            auto ping=publicPingMs.find(address.s_addr);
            if(ping!=publicPingMs.end()){host.pingMs=ping->second.milliseconds;host.pingKnown=ping->second.fresh(now);host.pingSampledUs=ping->second.sampledUs;}
            hosts.push_back(host);
        }
    }
};

Session::Session(std::function<void()> connectionPump)
    : impl(new Impl())
{
    impl->connectionPump = std::move(connectionPump);
}

Session::~Session()
{
    stop();
    delete impl;
}

std::string Session::formatError(const char* operation, int code)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s failed (0x%08x)", operation, static_cast<unsigned>(code));
    return text;
}

bool Session::initialize(std::string& error)
{
    if (impl->matchingInitialized)
        return true;

    // Reserve callback ownership BEFORE touching process-wide SDK resources.
    // A second Session must not initialize/terminate the first one's matcher.
    {
        std::lock_guard<std::mutex> lock(callbackOwnerMutex);
        if (callbackOwner && callbackOwner != this)
        {
            error = "Another Vita ad hoc session is already active";
            return false;
        }
        callbackOwner = this;
        callbackOwnerStopping = false;
    }

    // VitaShell uses the same PSPNet Adhoc + matching stack on retail Vita.
    // Do not own or tear down a subsystem that another library initialized.
    const int pspnetModuleResult = sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC);
    if (pspnetModuleResult < 0)
    {
        error = formatError("PSPNet ad hoc module", pspnetModuleResult);
        return false;
    }
    impl->pspnetModuleLoaded = true;

    const int adhocResult = sceNetAdhocInit();
    if (adhocResult < 0 && adhocResult != SCE_ERROR_NET_ADHOC_ALREADY_INITIALIZED)
    {
        error = formatError("sceNetAdhocInit", adhocResult);
        return false;
    }
    impl->ownsAdhoc = adhocResult == 0;

    SceNetAdhocctlAdhocId adhocId = {};
    adhocId.type = SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED;
    static const char titleId[] = "RBXV00001";
    std::memcpy(adhocId.data, titleId, SCE_NET_ADHOCCTL_ADHOCID_LEN);
    const int ctlResult = sceNetAdhocctlInit(&adhocId);
    if (ctlResult < 0 && ctlResult != SCE_ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED)
    {
        error = formatError("sceNetAdhocctlInit", ctlResult);
        if (impl->ownsAdhoc)
            sceNetAdhocTerm();
        impl->ownsAdhoc = false;
        return false;
    }
    impl->ownsAdhocCtl = ctlResult == 0;

    const int moduleResult = sceSysmoduleLoadModule(SCE_SYSMODULE_NET_ADHOC_MATCHING);
    if (moduleResult < 0)
    {
        error = formatError("Ad hoc matching module", moduleResult);
        return false;
    }
    impl->moduleLoaded = true;

    const unsigned poolSize = 256 * 1024;
    impl->pool = memalign(64, poolSize);
    if (!impl->pool)
    {
        error = "Not enough memory for Vita ad hoc matching";
        return false;
    }
    std::memset(impl->pool, 0, poolSize);

    const int result = sceNetAdhocMatchingInit(poolSize, impl->pool);
    if (result < 0 && result != SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_INITIALIZED)
    {
        error = formatError("sceNetAdhocMatchingInit", result);
        free(impl->pool);
        impl->pool = nullptr;
        return false;
    }
    impl->ownsMatching = result == 0;
    impl->matchingInitialized = true;
    return true;
}

bool Session::prepareNativeAdhoc(std::string& error)
{
    if (!initialize(error))
    {
        stop();
        return false;
    }

    // Retail Vita needs the system connection dialog to establish the native
    // ad hoc interface. PSPNet initialization and matching do not connect it.
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlAdhocGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED)
    {
        SceNetCheckDialogParam param;
        sceNetCheckDialogParamInit(&param);
        param.mode = SCE_NETCHECK_DIALOG_MODE_ADHOC_CONN;
        std::memcpy(param.npCommunicationId.data, "RBXV00001", 9);
        const int started = sceNetCheckDialogInit(&param);
        if (started < 0)
        {
            error = formatError("Offline connection dialog", started);
            stop();
            return false;
        }
        impl->ownsNativeConnection = true;
        impl->ownsConnectionDialog = true;
        // This includes the system confirmation prompt, not just radio setup.
        const uint64_t dialogDeadline = sceKernelGetSystemTimeWide() + 60000000ULL;
        while (sceNetCheckDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING &&
               sceKernelGetSystemTimeWide() < dialogDeadline)
        {
            if (impl->connectionPump) impl->connectionPump();
            sceKernelDelayThread(16000);
        }
        SceNetCheckDialogResult result = {};
        const bool finished = sceNetCheckDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED;
        if (!finished) sceNetCheckDialogAbort();
        const bool connected = finished && sceNetCheckDialogGetResult(&result) >= 0 &&
            result.result == SCE_COMMON_DIALOG_RESULT_OK;
        sceNetCheckDialogTerm();
        impl->ownsConnectionDialog = false;
        if (!connected)
        {
            error = finished ? "Offline connection cancelled." : "Offline connection timed out.";
            stop();
            return false;
        }
    }

    // Start matching privately; publish the real hello only after gameplay is
    // ready. This keeps the transport stable before the original socket bind.
    Advertisement pending;
    if (!createMatcher(impl->hosting ? SCE_NET_ADHOC_MATCHING_MODE_PARENT :
            SCE_NET_ADHOC_MATCHING_MODE_CHILD, pending, error, false))
        return false;

    const uint64_t deadline = sceKernelGetSystemTimeWide() + 1500000ULL;
    do
    {
        int state = SCE_NETCTL_STATE_DISCONNECTED;
        sceNetCtlCheckCallback();
        if (sceNetCtlAdhocGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED)
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->transport = Impl::TransportNativeAdhoc;
            impl->transportPrepared = true;
            impl->lastStatus = "Vita direct ad hoc transport ready";
            return true;
        }
        sceKernelDelayThread(20000);
    }
    while (sceKernelGetSystemTimeWide() < deadline);

    error = "Vita direct ad hoc did not become ready";
    SceNetInAddr address = {};
    const int addressResult = sceNetCtlAdhocGetInAddr(&address);
    int finalState = SCE_NETCTL_STATE_DISCONNECTED;
    const int stateResult = sceNetCtlAdhocGetState(&finalState);
    std::fprintf(stderr, "Vita ad hoc readiness: state result=%08x state=%d address result=%08x address=%08x\n",
        unsigned(stateResult), finalState, unsigned(addressResult), unsigned(address.s_addr));
    stop();
    return false;
}

bool Session::prepareHostTransport(std::string& error, bool allowLanFallback)
{
    stop();
    impl->hosting = true;

    std::string nativeError;
    if (prepareNativeAdhoc(nativeError))
    {
        impl->hosting = true;
        return true;
    }

    if (nativeError == "Offline connection cancelled.") { error = nativeError; return false; }

    int inetState = SCE_NETCTL_STATE_DISCONNECTED;
    if (allowLanFallback && sceNetCtlInetGetState(&inetState) >= 0 && inetState == SCE_NETCTL_STATE_CONNECTED)
    {
        std::fprintf(stderr, "Vita host using Wi-Fi fallback: %s\n", nativeError.c_str());
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->hosting = true;
        impl->transport = Impl::TransportLan;
        impl->transportPrepared = true;
        impl->lastStatus = "Using local Wi-Fi multiplayer fallback";
        return true;
    }

    error = nativeError.empty() ? "Vita ad-hoc is unavailable" : nativeError;
    return false;
}

bool Session::createMatcher(int mode, const Advertisement& hello, std::string& error, bool advertise)
{
    if (!initialize(error))
    {
        stop();
        return false;
    }

    const int id = sceNetAdhocMatchingCreate(
        static_cast<SceNetAdhocMatchingMode>(mode), kMaxPeers, kMatchingPort,
        64 * 1024, 750000, 1500000, 3, 500000, &Session::matchingCallback);
    if (id < 0)
    {
        error = formatError("sceNetAdhocMatchingCreate", id);
        stop();
        return false;
    }
    impl->matchingId = id;
    impl->localAdvertisement = hello;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->transport = Impl::TransportNativeAdhoc;
        impl->transportPrepared = true;
    }

    std::vector<unsigned char> payload = encodeAdvertisement(hello);
    const int startResult = sceNetAdhocMatchingStart(
        id, 0x40, 0x4000, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT,
        advertise ? static_cast<int>(payload.size()) : 0, advertise ? payload.data() : nullptr);
    if (startResult < 0)
    {
        error = formatError("sceNetAdhocMatchingStart", startResult);
        sceNetAdhocMatchingDelete(id);
        impl->matchingId = -1;
        stop();
        return false;
    }
    std::fprintf(stderr,"Vita native matching started id=%d mode=%d port=%u\n",id,mode,unsigned(kMatchingPort));
    return true;
}

bool Session::startLan(const Advertisement& value, bool hosting, std::string& error)
{
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED)
    {
        error = "Wi-Fi is disconnected. Connect to Wi-Fi in Vita settings, then refresh.";
        return false;
    }
    const int socket = sceNetSocket("Bloxicon Vita servers", SCE_NET_AF_INET,
        SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
    if (socket < 0) { error = formatError("LAN discovery socket", socket); return false; }
    const int enabled = 1;
    if (sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO,
        &enabled, sizeof(enabled)) < 0)
    {
        RBX::Network::vitaSocketClose(socket);
        error = "Could not configure nonblocking server discovery.";
        return false;
    }
    if (hosting && sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST,
        &enabled, sizeof(enabled)) < 0)
    {
        RBX::Network::vitaSocketClose(socket);
        error = "Could not enable Wi-Fi server announcements.";
        return false;
    }
    // Both roles bind the discovery port. Hosts must receive liveness probes;
    // clients receive periodic advertisements. Devices have separate IPs so
    // sharing the same well-known port across consoles is intentional.
    {
        SceNetSockaddrIn bindAddress = {};
        bindAddress.sin_len = sizeof(bindAddress);
        bindAddress.sin_family = SCE_NET_AF_INET;
        bindAddress.sin_port = sceNetHtons(kMatchingPort);
        bindAddress.sin_addr.s_addr = SCE_NET_INADDR_ANY;
        const int result = sceNetBind(socket,
            reinterpret_cast<const SceNetSockaddr*>(&bindAddress), sizeof(bindAddress));
        if (result < 0)
        {
            RBX::Network::vitaSocketClose(socket);
            error = formatError("LAN discovery bind", result);
            return false;
        }
    }
    impl->lanMode = true;
    impl->lanSocket = socket;
    impl->localAdvertisement = value;
    impl->hosting = hosting;
    impl->lanRunning = true;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->transport = Impl::TransportLan;
        impl->transportPrepared = true;
        impl->lastStatus = hosting ? "Advertising server on local Wi-Fi" :
            "Searching for Bloxicon Vita servers on local Wi-Fi";
    }
    try
    {
    impl->lanThread = std::thread([this, hosting, value, socket]() {
        try {
        // Preserve the original 128-byte announcement for older clients.

        SceNetSockaddrIn broadcast = {};
        broadcast.sin_len = sizeof(broadcast);
        broadcast.sin_family = SCE_NET_AF_INET;
        broadcast.sin_port = sceNetHtons(kMatchingPort);
        broadcast.sin_addr.s_addr = SCE_NET_INADDR_BROADCAST;
        std::vector<SceNetSockaddrIn> publicHosts;
        std::map<unsigned int,std::string> publicNames;
        std::map<unsigned int,std::string> publicProfiles;
        auto endpoints=savedServerEndpoints();
        std::string accountHost;unsigned short accountPort=0;
        if(RBX::Network::vitaOnlineService(accountHost,accountPort)) {
            ServerEndpoint endpoint;endpoint.host=accountHost;endpoint.port=accountPort;
            endpoint.name="Bloxicon online";endpoints={endpoint};
        } else endpoints.clear();
        std::vector<bool> configured(endpoints.size(),false);
        std::vector<uint64_t> retryAt(endpoints.size(),0);
        struct DnsResult { int status=-1; SceNetInAddr address{}; };
        std::future<DnsResult> pendingDns;
        // Declared after the future so cancellation also precedes its joining
        // destructor if an allocation or SDK wrapper throws in the LAN loop.
        struct CancelDnsOnExit {
            Impl* state;
            ~CancelDnsOnExit() {
                state->lanRunning=false;
                std::lock_guard<std::mutex> lock(state->resolverMutex);
                if(state->publicResolver>=0)sceNetResolverAbort(state->publicResolver,0);
            }
        } cancelDns{impl};
        size_t resolvingProfile=0, nextDnsProfile=0;
        uint64_t nextPublicProbe=0;
        auto configurePublicHost=[&](size_t row, const SceNetInAddr& address) {
            const auto& endpoint=endpoints[row];
            SceNetSockaddrIn publicHost=broadcast;
            publicHost.sin_addr=address;
            if(std::any_of(publicHosts.begin(),publicHosts.end(),[&](const SceNetSockaddrIn& h){return h.sin_addr.s_addr==address.s_addr;}))return;
            if(!RBX::Network::vitaConfigureAccountTunnel(address.s_addr,endpoint.port,endpoint.host))return;
            std::fprintf(stderr,"Vita configured saved server route\n");
            publicHosts.push_back(publicHost);
            publicNames[address.s_addr]=serverDisplayName(endpoint,row);configured[row]=true;
            publicProfiles[address.s_addr]=endpoint.text();
            nextPublicProbe=0;
        };
        SceNetInAddr bootstrap{};
        const bool official=accountPort==31901&&VitaOfficialEndpoint::matches(accountHost.c_str());
        if(official){
            std::memcpy(&bootstrap.s_addr,VitaOfficialEndpoint::ipv4.data(),4);
            configurePublicHost(0,bootstrap);
        }
        bool waitingForBootstrap=official&&configured[0];
        const uint64_t bootstrapDeadline=sceKernelGetSystemTimeWide()+
            RBX::Network::VitaOnlineTiming::standardPortProbeUs+RBX::Network::VitaOnlineTiming::webSocketProbeUs;
        LanControl availability;
        availability.serverPort = kRobloxServerPort;
        std::map<unsigned int,ServerSnapshotState<Advertisement>> snapshots;
        std::array<unsigned char, 512> packet = {};
        uint64_t nextAdvertisement = 0;
        while (impl->lanRunning)
        {
            const uint64_t now = sceKernelGetSystemTimeWide();
            if(!hosting) {
                if(waitingForBootstrap&&now>=bootstrapDeadline&&
                   !RBX::Network::vitaTcpTunnelEnabled(bootstrap.s_addr)) {
                    // Keep probing the published address while allowing the
                    // system resolver to discover a migrated official route.
                    configured[0]=false;
                    waitingForBootstrap=false;
                }
                // DNS may take seconds or stall on a disconnected network. Only
                // one lookup runs at a time; the receive/probe loop never waits
                // for it, and stop() can still abort the registered resolver.
                if(pendingDns.valid() && pendingDns.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                    const DnsResult result=pendingDns.get();
                    if(result.status>=0) {
                        std::array<unsigned char,4> address{};
                        std::memcpy(address.data(),&result.address.s_addr,4);
                        if(parseServerIPv4(std::to_string(address[0])+"."+std::to_string(address[1])+"."+
                            std::to_string(address[2])+"."+std::to_string(address[3]),address))
                            configurePublicHost(resolvingProfile,result.address);
                    } else if(impl->lanRunning) {
                        std::fprintf(stderr,"Vita server DNS failed result=%08x\n",unsigned(result.status));
                    }
                }
                // Numeric endpoints need no worker and remain usable even if
                // an earlier hostname is waiting for its DNS timeout.
                for(size_t row=0;row<endpoints.size() && impl->lanRunning;++row) {
                    if(configured[row] || !endpoints[row].enabled || now<retryAt[row])continue;
                    std::array<unsigned char,4> address{};
                    if(!parseServerIPv4(endpoints[row].host,address))continue;
                    retryAt[row]=now+15000000;
                    SceNetInAddr nativeAddress{};
                    std::memcpy(&nativeAddress.s_addr,address.data(),4);
                    configurePublicHost(row,nativeAddress);
                }
                if(!pendingDns.valid())for(size_t checked=0;checked<endpoints.size() && impl->lanRunning;++checked) {
                    const size_t row=nextDnsProfile++ % endpoints.size();
                    std::array<unsigned char,4> address{};
                    if(configured[row] || !endpoints[row].enabled || now<retryAt[row] ||
                        parseServerIPv4(endpoints[row].host,address))continue;
                    retryAt[row]=now+15000000;
                    resolvingProfile=row;
                    const std::string host=endpoints[row].host;
                    try {
                        pendingDns=std::async(std::launch::async,[this,host]() {
                            DnsResult result;
                            const int resolver=sceNetResolverCreate("Bloxicon server DNS",nullptr,0);
                            if(resolver<0){result.status=resolver;return result;}
                            {
                                std::lock_guard<std::mutex> lock(impl->resolverMutex);
                                if(!impl->lanRunning){sceNetResolverDestroy(resolver);return result;}
                                impl->publicResolver=resolver;
                            }
                            // Sony uses microseconds; 1 is rejected on hardware.
                            result.status=sceNetResolverStartNtoa(resolver,host.c_str(),&result.address,3000000,2,0);
                            {
                                std::lock_guard<std::mutex> lock(impl->resolverMutex);
                                impl->publicResolver=-1;sceNetResolverDestroy(resolver);
                            }
                            return result;
                        });
                    } catch(const std::exception& e) {
                        std::fprintf(stderr,"Vita server DNS worker unavailable: %s\n",e.what());
                    }
                    break;
                }
            }
            if (!hosting && now >= nextPublicProbe)
            {
                for(const auto& publicHost:publicHosts)
                {
                    const uint64_t sentAt = sceKernelGetSystemTimeWide();
                    availability.nonce = sentAt | 1ULL;
                    {std::lock_guard<std::mutex> lock(impl->mutex);impl->publicPingMs[publicHost.sin_addr.s_addr].sent(availability.nonce,sentAt);}
                    const auto publicProbe = encodeLanControl(availability);
                    RBX::Network::vitaProbeTcpTunnel(socket, &publicHost, sizeof(publicHost), publicProbe.data(), unsigned(publicProbe.size()));
                }
                nextPublicProbe = sceKernelGetSystemTimeWide() + 1000000;
            }
            if (hosting && now >= nextAdvertisement)
            {
                Advertisement current;
                { std::lock_guard<std::mutex> lock(impl->mutex); current=impl->localAdvertisement; }
                const auto roster=encodeAdvertisement(current);
                current.rosterKnown=false;
                const auto advertisement=encodeAdvertisement(current);
                if(roster.size()>advertisement.size()) RBX::Network::vitaSendto(socket,roster.data(),unsigned(roster.size()),0,reinterpret_cast<const SceNetSockaddr*>(&broadcast),sizeof(broadcast));
                RBX::Network::vitaSendto(socket, advertisement.data(), unsigned(advertisement.size()), 0,
                    reinterpret_cast<const SceNetSockaddr*>(&broadcast), sizeof(broadcast));
                nextAdvertisement = now + 500000;
            }

            // A busy host sends base, roster and quota advertisements for each
            // game. Drain a bounded burst so those replies do not queue behind
            // the old one-packet/20ms limit.
            for (unsigned drain=0; drain<16 && impl->lanRunning; ++drain) {
            SceNetSockaddrIn sender = {};
            unsigned senderLength = sizeof(sender);
            const int received = RBX::Network::vitaRecvfrom(socket, packet.data(), unsigned(packet.size()),
                SCE_NET_MSG_DONTWAIT, reinterpret_cast<SceNetSockaddr*>(&sender), &senderLength);
            if (received <= 0) break;
            if (received > 0 && sender.sin_family == SCE_NET_AF_INET)
            {
                if (hosting)
                {
                    LanControl probe;
                    if (decodeLanControl(packet.data(), size_t(received), probe) &&
                        probe.type == LanProbe &&
                        probe.placeHash == value.placeHash &&
                        probe.serverPort == value.serverPort)
                    {
                        probe.type = LanAck;
                        const std::vector<unsigned char> ack = encodeLanControl(probe);
                        RBX::Network::vitaSendto(socket, ack.data(), unsigned(ack.size()), 0,
                            reinterpret_cast<const SceNetSockaddr*>(&sender), senderLength);
                    }
                }
                else
                {
                    const uint64_t receivedAt=sceKernelGetSystemTimeWide();
                    const bool publicSender=std::any_of(publicHosts.begin(),publicHosts.end(),[&](const SceNetSockaddrIn& endpoint){return endpoint.sin_addr.s_addr==sender.sin_addr.s_addr;});
                    LanControl ack;
                    if(publicSender && decodeLanControl(packet.data(),size_t(received),ack) && ack.serverPort==kRobloxServerPort) {
                        if(ack.type==LanAck && ack.placeHash==0) {
                            std::lock_guard<std::mutex> lock(impl->mutex);
                            auto ping=impl->publicPingMs.find(sender.sin_addr.s_addr);
                            if(ping!=impl->publicPingMs.end() && ping->second.acknowledge(ack.nonce,receivedAt)) {
                                for(auto& known:impl->hosts)if(known.address.s_addr==sender.sin_addr.s_addr){
                                    known.pingMs=ping->second.milliseconds;known.pingKnown=true;known.pingSampledUs=receivedAt;
                                }
                                if(ack.features&1)snapshots[sender.sin_addr.s_addr].begin(ack.nonce,receivedAt);
                                else snapshots.erase(sender.sin_addr.s_addr);
                            }
                        } else if(ack.type==LanSnapshotEnd && (ack.features&1)) {
                            auto snapshot=snapshots.find(sender.sin_addr.s_addr);std::vector<Advertisement> rows;
                            if(snapshot!=snapshots.end() && snapshot->second.finish(ack.nonce,ack.placeHash,receivedAt,rows)) {
                                std::lock_guard<std::mutex> lock(impl->mutex);
                                impl->hosts.erase(std::remove_if(impl->hosts.begin(),impl->hosts.end(),[&](const DiscoveredHost& host){return host.address.s_addr==sender.sin_addr.s_addr;}),impl->hosts.end());
                                for(const auto& row:rows)impl->rememberHost(sender.sin_addr,row,receivedAt,publicProfiles[sender.sin_addr.s_addr]);
                                impl->lastStatus="Server list updated";
                            }
                        }
                    }
                    Advertisement remote;
                    if(decodeAdvertisement(packet.data(),size_t(received),remote)) {
                        // Keep manager changes visible; redact only presentation
                        // text, never replace the host's name with a saved label.
                        remote.hostName=privateServerText(remote.hostName);
                        const auto named=publicNames.find(sender.sin_addr.s_addr);
                        if(remote.hostName.empty() && named!=publicNames.end())remote.hostName=named->second;
                        auto snapshot=snapshots.find(sender.sin_addr.s_addr);
                        if(publicSender && snapshot!=snapshots.end() && snapshot->second.active(receivedAt))snapshot->second.add(std::move(remote));
                        else {
                            std::lock_guard<std::mutex> lock(impl->mutex);
                            const auto source=publicProfiles.find(sender.sin_addr.s_addr);
                            impl->rememberHost(sender.sin_addr,remote,receivedAt,source==publicProfiles.end()?std::string():source->second);
                            impl->lastStatus="Bloxicon server found";
                        }
                    }
                }
            }
            }
            sceKernelDelayThread(20000);
        }
        } catch(const std::exception& error) {
            std::fprintf(stderr,"Vita discovery worker failed: %s\n",error.what());
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->terminalError="Server discovery ran out of resources. Refresh to retry.";
            impl->lanRunning=false;
        }
    });
    }
    catch (const std::exception&)
    {
        error = "Not enough resources to start server discovery.";
        stop();
        return false;
    }
    return true;
}

namespace {
int transferSocket(bool native) {
    const int socket=sceNetSocket("Bloxicon place transfer", SCE_NET_AF_INET,
        native?SCE_NET_SOCK_DGRAM_P2P:SCE_NET_SOCK_DGRAM,native?0:SCE_NET_IPPROTO_UDP);
    if(socket<0) return socket;
    const int enabled=1;
    if(sceNetSetsockopt(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,&enabled,sizeof(enabled))<0) {
        RBX::Network::vitaSocketClose(socket); return -1;
    }
    return socket;
}
void transferPort(SceNetSockaddrIn& address, unsigned short port, bool native) {
    // Gameplay owns physical port 3658. Transfers use their dedicated physical
    // endpoint as well as a virtual port; retail Vita rejects a second bind to
    // gameplay's physical endpoint even when SO_REUSEPORT succeeds.
    address.sin_port=sceNetHtons(port);
    address.sin_vport=native?sceNetHtons(port):0;
}
uint64_t sharedFileHash(const std::string& path) {
    std::ifstream input(path.c_str(),std::ios::binary); uint64_t hash=1469598103934665603ULL;
    char buffer[8192];
    while(input) { input.read(buffer,sizeof(buffer)); for(std::streamsize i=0;i<input.gcount();++i) {hash^=static_cast<unsigned char>(buffer[i]);hash*=1099511628211ULL;} }
    return input.eof()?hash:0;
}
}

bool Session::startSharing(const std::string& path,uint64_t hash,std::string& error,
                           const std::vector<std::string>& assets) {
    if(impl->sharing) return true;
    const int socket=transferSocket(usesNativeAdhocTransport());
    if(socket<0) {error="Could not share this game.";return false;}
    SceNetSockaddrIn local={}; local.sin_len=sizeof(local);local.sin_family=SCE_NET_AF_INET;
    transferPort(local,Transfer::port,usesNativeAdhocTransport());local.sin_addr.s_addr=SCE_NET_INADDR_ANY;
    const int bindResult=sceNetBind(socket,reinterpret_cast<SceNetSockaddr*>(&local),sizeof(local));
    if(bindResult<0) {
#ifdef __vita__
        const int bindErrno=*sceNetErrnoLoc();
#else
        const int bindErrno=0;
#endif
        std::fprintf(stderr,"Vita game sharing bind failed result=%08x errno=%d native=%d socket=%d physical=%u virtual=%u\n",
            unsigned(bindResult),bindErrno,int(usesNativeAdhocTransport()),socket,unsigned(sceNetHtons(local.sin_port)),unsigned(sceNetHtons(local.sin_vport)));
        std::fflush(stderr);
        RBX::Network::vitaSocketClose(socket);error="Could not open game sharing.";return false;
    }
    impl->shareSocket=socket;impl->sharing=true;
    try { impl->shareThread=std::thread([this,path,hash,socket,assets] {
        // Only the selected place and flat cached assets are readable remotely.
        // The client never supplies a filesystem path.
        std::vector<std::string> files={path,std::string()};std::ostringstream listing;
        const std::string root="ux0:/data/Bloxicon/assets/";
        for(const auto& name:assets) {
            if(!impl->sharing || files.size()>=514) break;
            if(!Transfer::safeAssetName(name)) continue;
            std::ifstream asset(root+name,std::ios::binary|std::ios::ate);
            if(!asset || asset.tellg()<=0 || uint64_t(asset.tellg())>Transfer::maxFileSize) continue;
            const auto size=uint64_t(asset.tellg());asset.close();
            const auto assetHash=sharedFileHash(root+name);
            if(assetHash) {files.push_back(root+name);listing<<name<<'\t'<<size<<'\t'<<assetHash<<'\n';}
        }
        const std::string manifest=listing.str();
        std::ifstream input;uint64_t openedFile=~uint64_t(0),openedSize=0;
        std::array<unsigned char,Transfer::headerSize+Transfer::chunkSize> request={};
        while(impl->sharing) {
            SceNetSockaddrIn sender={};unsigned length=sizeof(sender);
            const int received=RBX::Network::vitaRecvfrom(socket,request.data(),request.size(),SCE_NET_MSG_DONTWAIT,
                reinterpret_cast<SceNetSockaddr*>(&sender),&length);
            if(received==int(Transfer::headerSize) && Transfer::valid(request.data(),received) &&
               Transfer::get(request.data(),8,8)==hash) {
                const auto file=Transfer::get(request.data(),4,4),offset=Transfer::get(request.data(),16,8);
                if(file<files.size()) {
                    uint64_t total=manifest.size();
                    if(file!=1) {
                        if(openedFile!=file) {
                            input.close();input.clear();input.open(files[file].c_str(),std::ios::binary|std::ios::ate);
                            if(!input || input.tellg()<0) {openedFile=~uint64_t(0);continue;}
                            openedFile=file;openedSize=uint64_t(input.tellg());
                        }
                        total=openedSize;
                    }
                    if(total>Transfer::maxFileSize || offset>total || offset%Transfer::chunkSize) continue;
                    const size_t count=std::min(uint64_t(Transfer::chunkSize),total-offset);
                    auto response=Transfer::packet(hash,file,offset,total,count);
                    if(file==1) std::memcpy(response.data()+Transfer::headerSize,manifest.data()+offset,count);
                    else {input.clear();input.seekg(offset);input.read(reinterpret_cast<char*>(response.data()+Transfer::headerSize),count);if(size_t(input.gcount())!=count) continue;}
                    RBX::Network::vitaSendto(socket,response.data(),response.size(),0,reinterpret_cast<SceNetSockaddr*>(&sender),length);
                }
            } else sceKernelDelayThread(1000);
        }
    }); } catch(...) {impl->sharing=false;RBX::Network::vitaSocketClose(socket);impl->shareSocket=-1;error="Could not start game sharing.";return false;}
    return true;
}

bool Session::isVerifiedAccountHost(const DiscoveredHost& host) const
{
    return usesLanTransport() && RBX::Network::vitaVerifiedAccountTcpRoute(host.address.s_addr);
}

void Session::endFileBatch()
{
    if(impl->receiveSocket>=0)RBX::Network::vitaSocketClose(impl->receiveSocket);
    impl->receiveSocket=-1;impl->receiveAddress={};impl->receiveHash=0;
    impl->receiveDeadline=0;impl->receiveGamePort=0;
    impl->receiveAccount=false;impl->receiveResponded=false;
}

bool Session::beginFileBatch(const DiscoveredHost& host,std::string& error,const std::function<bool()>& continuation)
{
    if(impl->receiveSocket>=0) {error="A game transfer is already active.";return false;}
    if(continuation&&!continuation()) {error="Transfer cancelled.";return false;}
    bool selectedVerified=false;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        selectedVerified=impl->lanMode&&impl->connectionEstablished&&
            sameAddress(impl->connectedPeer,host.address)&&
            impl->peerAdvertisement.placeHash==host.advertisement.placeHash&&
            impl->peerAdvertisement.serverPort==host.advertisement.serverPort;
    }
    // Selection already established the nonce-bound host. Otherwise verify
    // with cancellation active before opening a batch admission.
    if(usesLanTransport()&&!selectedVerified&&!verifyLanHost(host,error,continuation))return false;
    const bool native=usesNativeAdhocTransport();
    const int socket=transferSocket(native);
    if(socket<0) {error="Could not receive this game.";return false;}
    SceNetSockaddrIn local={};local.sin_len=sizeof(local);local.sin_family=SCE_NET_AF_INET;
    transferPort(local,native?Transfer::port+1:0,native);
    if(sceNetBind(socket,reinterpret_cast<SceNetSockaddr*>(&local),sizeof(local))<0) {
        RBX::Network::vitaSocketClose(socket);error="Could not open game transfer.";return false;
    }
    impl->receiveSocket=socket;impl->receiveAddress=host.address;
    impl->receiveHash=host.advertisement.placeHash;impl->receiveGamePort=host.advertisement.serverPort;
    impl->receiveDeadline=sceKernelGetSystemTimeWide()+900000000ULL;
    impl->receiveAccount=isVerifiedAccountHost(host);impl->receiveResponded=false;
    return true;
}

bool Session::receiveFile(const DiscoveredHost& host,uint32_t file,const std::string& destination,
    const std::function<bool(uint64_t,uint64_t)>& progress,std::string& error,uint64_t maximumBytes) {
    if(maximumBytes>Transfer::maxFileSize) {error="Invalid game transfer bound.";return false;}
    const bool ownsBatch=impl->receiveSocket<0;
    if(ownsBatch&&!beginFileBatch(host,error,[&](){return !progress||progress(0,0);}))return false;
    bool success=false;
    struct BatchGuard {Session& session;bool owns;bool& success;
        ~BatchGuard(){if(owns||!success)session.endFileBatch();}} guard{*this,ownsBatch,success};
    if(impl->receiveAddress.s_addr!=host.address.s_addr || impl->receiveHash!=host.advertisement.placeHash ||
       impl->receiveGamePort!=host.advertisement.serverPort ||
       (impl->receiveAccount&&!isVerifiedAccountHost(host))) {
        error="The verified game transfer changed. Please reconnect.";return false;
    }
    const int socket=impl->receiveSocket;
    SceNetSockaddrIn peer={};peer.sin_len=sizeof(peer);peer.sin_family=SCE_NET_AF_INET;
    peer.sin_addr=host.address;transferPort(peer,Transfer::port,usesNativeAdhocTransport());
    const int descriptor=::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(descriptor<0) {error="Could not create a new transfer file; existing bytes were preserved.";return false;}
    FILE* output=::fdopen(descriptor,"wb");
    if(!output){::close(descriptor);std::remove(destination.c_str());error="Could not save the received game.";return false;}
    struct OutputGuard {FILE*& output;const std::string& path;bool& success;
        ~OutputGuard(){if(output)std::fclose(output);if(!success)std::remove(path.c_str());}} outputGuard{output,destination,success};
    uint64_t offset=0,total=0;bool first=true;uint64_t nextProgress=0;
    const uint64_t transferStarted=sceKernelGetSystemTimeWide();
    const uint64_t transferDeadline=impl->receiveDeadline;
    // Keep native ad-hoc's conservative window; Wi-Fi pipelines enough original
    // bytes to cover Internet latency without allocating per-datagram objects.
    const unsigned window=usesNativeAdhocTransport()?32:Transfer::Window::capacity;
    std::unique_ptr<Transfer::Window> pending(new(std::nothrow) Transfer::Window);
    if(!pending) {error="Not enough memory for game transfer.";return false;}
    while(first || offset<total) {
        bool accepted=false;
        for(unsigned retry=0;retry<(impl->receiveAccount&&!impl->receiveResponded?
                unsigned(RBX::Network::VitaOnlineTiming::publicPrejoinUs/250000ULL):40u) && !accepted;++retry) {
            if(retry) pending->retry();
            for(unsigned slot=0;slot<(first?1:window);++slot) {
                const uint64_t position=offset+slot*Transfer::chunkSize;
                if(!first && position>=total) break;
                if(pending->contains(position)) continue;
                auto request=Transfer::requestPacket(host.advertisement.placeHash,file,position);
                // Explicit SHA256/batch catalogue opt-in; old hosts ignore
                // this reserved field and return their legacy empty manifest.
                if(file&&impl->receiveAccount)Transfer::put(request,24,0x5242585441534841ULL,8);
                if(RBX::Network::vitaSendto(socket,request.data(),request.size(),0,reinterpret_cast<SceNetSockaddr*>(&peer),sizeof(peer))==int(request.size()))
                    pending->request(position);
            }
            const auto deadline=sceKernelGetSystemTimeWide()+250000;
            while(sceKernelGetSystemTimeWide()<deadline) {
                const auto now=sceKernelGetSystemTimeWide();
                if(now>=transferDeadline) {error="Game transfer timed out. Please try again.";return false;}
                if(progress && now>=nextProgress) {nextProgress=now+50000;if(!progress(offset,total)) {error="Transfer cancelled.";return false;}}
                std::array<unsigned char,Transfer::headerSize+Transfer::chunkSize> bytes={};
                SceNetSockaddrIn sender={};unsigned length=sizeof(sender);
                const int received=RBX::Network::vitaRecvfrom(socket,bytes.data(),bytes.size(),SCE_NET_MSG_DONTWAIT,reinterpret_cast<SceNetSockaddr*>(&sender),&length);
                if(received>=int(Transfer::headerSize) && sender.sin_addr.s_addr==peer.sin_addr.s_addr && sender.sin_port==peer.sin_port && sender.sin_vport==peer.sin_vport &&
                   Transfer::valid(bytes.data(),received) && Transfer::get(bytes.data(),4,4)==file &&
                   Transfer::get(bytes.data(),8,8)==host.advertisement.placeHash) {
                    const uint64_t position=Transfer::get(bytes.data(),16,8);
                    const auto advertised=Transfer::get(bytes.data(),24,8);
                    if(advertised>maximumBytes) {error="Server transfer exceeds its allowed size.";return false;}
                    if((!first && advertised!=total) || position>advertised ||
                       position<offset || position>=offset+window*Transfer::chunkSize || position%Transfer::chunkSize ||
                       !pending->outstanding(position)) continue;
                    const size_t count=received-Transfer::headerSize;
                    if(count!=std::min(uint64_t(Transfer::chunkSize),advertised-position)) continue;
                    total=advertised;first=false;impl->receiveResponded=true;
                    if(!pending->accept(position,bytes.data()+Transfer::headerSize,count)) continue;
                    const auto* next=pending->next(offset);
                    while(next) {
                        const size_t written=std::fwrite(next->bytes.data(),1,next->count,output);
                        if(written!=next->count) {error="Could not save the received game.";return false;}
                        const size_t consumed=next->count;
                        pending->consume(offset);offset+=consumed;accepted=true;
                        next=pending->next(offset);
                    }
                    // Drain replies already available before refilling the
                    // window. Refilling after every single chunk turns a
                    // pipelined TLS transfer back into tiny individual writes.
                    if(offset==total) break;
                    continue;
                }
                if(accepted) break;
                sceKernelDelayThread(1000);
            }
        }
        if(!accepted) {error="Game transfer stopped. Refresh and try again.";return false;}
    }
    const int closed=std::fclose(output);output=nullptr;
    if(closed!=0) {error="Could not finish saving the game.";return false;}
    success=true;
    std::fprintf(stderr,"Vita file transfer complete: file=%u bytes=%llu seconds=%.3f window=%u\n",file,
        static_cast<unsigned long long>(total),double(sceKernelGetSystemTimeWide()-transferStarted)/1000000.,window);
    return true;
}

void Session::updateRoster(const std::vector<std::string>& names)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->localAdvertisement.rosterKnown=true;
    impl->localAdvertisement.playerNames.assign(names.begin(),names.begin()+std::min(size_t(8),names.size()));
}

bool Session::startHost(const Advertisement& value, std::string& error)
{
    // A host advertisement is tied to the gameplay socket transport chosen
    // before NetworkServer::Start. Re-entering startHost while matching or the
    // LAN worker is active would either replace a live std::thread (terminate)
    // or make discovery disagree with the already-bound RakNet socket.
    if ((impl->matchingId >= 0 && (!impl->hosting || impl->hostAdvertisementPublished)) ||
        impl->lanRunning.load() || impl->lanThread.joinable())
    {
        error = "A Vita multiplayer host is already active.";
        return false;
    }

    if (!impl->transportPrepared && !prepareHostTransport(error))
        return false;

    if (impl->transport == Impl::TransportLan)
        return startLan(value, true, error);

    // prepareHostTransport() is intentionally the only point that may choose
    // between native P2P and LAN. main.cpp calls it before RakNet binds the
    // gameplay socket. If matching fails later, fail this host attempt instead
    // of silently switching discovery to a transport that the socket cannot use.
    const std::vector<unsigned char> payload = encodeAdvertisement(value);
    // A peer can answer the hello as soon as the SDK publishes it.
    // Install the matching identity before allowing that callback to run.
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->localAdvertisement = value;
    }
    const int published = sceNetAdhocMatchingSetHelloOpt(impl->matchingId,
        static_cast<int>(payload.size()), const_cast<unsigned char*>(payload.data()));
    if (published < 0)
    {
        error = formatError("Publish Vita host", published);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->localAdvertisement = value;
        impl->hostAdvertisementPublished = true;
        impl->lastStatus = "Hosting Vita ad hoc session";
    }
    std::fprintf(stderr,"Vita native host published hash=%016llx game_port=%u\n",
        static_cast<unsigned long long>(value.placeHash),unsigned(value.serverPort));
    return true;
}

bool Session::startDiscovery(const Advertisement& value, std::string& error, bool preferLan)
{
    error.clear();
    stop();
    impl->hosting = false;
    Advertisement hello = value;
    if (hello.hostName.empty())
        hello.hostName = "VitaPlayer";
    if (hello.placeName.empty())
        hello.placeName = "Bloxicon Vita client";
    // PC hosts use infrastructure UDP. A working native interface must not
    // hide LAN servers simply because native matching initialized first.
    if (preferLan)
        return startLan(hello, false, error);
    std::string nativeError;
    if (prepareNativeAdhoc(nativeError))
    {
        // CHILD listens for the parent's HELLO. Publishing HELLO is a parent
        // operation and can reject CHILD with INVALID_MODE. Client metadata
        // travels in SelectTarget's JOIN payload when a host is selected.
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->localAdvertisement = hello;
        impl->lastStatus = "Searching for nearby Bloxicon Vita hosts";
        std::fprintf(stderr,"Vita native child listening hash=%016llx; metadata sent with JOIN\n",
            static_cast<unsigned long long>(hello.placeHash));
        return true;
    }

    if (nativeError == "Offline connection cancelled.") { error = nativeError; return false; }

    int inetState = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlInetGetState(&inetState) >= 0 && inetState == SCE_NETCTL_STATE_CONNECTED)
    {
        stop();
        impl->hosting = false;
        return startLan(hello, false, error);
    }

    error = nativeError.empty() ? "No Vita ad hoc or local Wi-Fi transport is available" : nativeError;
    return false;
}

std::vector<DiscoveredHost> Session::discoveredHosts() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    const uint64_t now=sceKernelGetSystemTimeWide();
    impl->expireHosts(now);
    for(auto& host:impl->hosts)
        if(host.pingKnown && (now<host.pingSampledUs || now-host.pingSampledUs>=ServerProbeState::timeoutUs))host.pingKnown=false;
    return impl->hosts;
}

bool Session::verifyLanHost(const DiscoveredHost& selected, std::string& error, const std::function<bool()>& continuation)
{
    const int socket = sceNetSocket("Bloxicon Vita server probe", SCE_NET_AF_INET,
        SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
    if (socket < 0)
    {
        error = formatError("LAN server probe socket", socket);
        return false;
    }

    const int enabled = 1;
    if (sceNetSetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO,
        &enabled, sizeof(enabled)) < 0)
    {
        RBX::Network::vitaSocketClose(socket);
        error = "Could not configure the Wi-Fi server probe.";
        return false;
    }

    SceNetSockaddrIn destination = {};
    destination.sin_len = sizeof(destination);
    destination.sin_family = SCE_NET_AF_INET;
    destination.sin_port = sceNetHtons(kMatchingPort);
    destination.sin_addr = selected.address;

    LanControl probe;
    probe.type = LanProbe;
    probe.nonce = sceKernelGetSystemTimeWide() ^
        (uint64_t(selected.address.s_addr) << 17) ^
        selected.advertisement.placeHash ^
        uint64_t(selected.advertisement.serverPort);
    if (!probe.nonce) probe.nonce = 1;
    probe.placeHash = selected.advertisement.placeHash;
    probe.serverPort = selected.advertisement.serverPort;
    const std::vector<unsigned char> payload = encodeLanControl(probe);

    // Discovery UDP can work while a router blocks the game's UDP port.
    // Verify TCP before creating RakNet, even when UDP discovery succeeded.
    // Never change a live gameplay connection's transport in the background.
    unsigned char octets[4];
    std::memcpy(octets, &selected.address.s_addr, sizeof(octets));
    const bool publicHost = octets[0] != 10 && octets[0] != 127 &&
        !(octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) &&
        !(octets[0] == 192 && octets[1] == 168) &&
        !(octets[0] == 169 && octets[1] == 254);
    const uint64_t started = sceKernelGetSystemTimeWide();
    const uint64_t deadline = started + (publicHost?RBX::Network::VitaOnlineTiming::publicPrejoinUs:3000000ULL);
    const uint64_t tcpWaitUntil = started + 3000000ULL;
    bool verified = false;
    uint64_t nextSend = 0;
    std::array<unsigned char, 128> packet = {};
    while (sceKernelGetSystemTimeWide() < deadline)
    {
        if(continuation&&!continuation()) {
            RBX::Network::vitaSocketClose(socket);error="Transfer cancelled.";return false;
        }
        const uint64_t now = sceKernelGetSystemTimeWide();
        if (now >= nextSend)
        {
            if (publicHost && !RBX::Network::vitaTcpTunnelEnabled(selected.address.s_addr))
                RBX::Network::vitaProbeTcpTunnel(socket, &destination, sizeof(destination), payload.data(), unsigned(payload.size()));
            RBX::Network::vitaSendto(socket, payload.data(), unsigned(payload.size()), 0,
                reinterpret_cast<const SceNetSockaddr*>(&destination), sizeof(destination));
            nextSend = now + 200000;
        }

        SceNetSockaddrIn sender = {};
        unsigned senderLength = sizeof(sender);
        const int received = RBX::Network::vitaRecvfrom(socket, packet.data(), unsigned(packet.size()),
            SCE_NET_MSG_DONTWAIT, reinterpret_cast<SceNetSockaddr*>(&sender), &senderLength);
        if (received > 0 && sender.sin_family == SCE_NET_AF_INET &&
            sameAddress(sender.sin_addr, selected.address) && sender.sin_port == destination.sin_port)
        {
            LanControl ack;
            if (decodeLanControl(packet.data(), size_t(received), ack) &&
                ack.type == LanAck && ack.nonce == probe.nonce &&
                ack.placeHash == probe.placeHash && ack.serverPort == probe.serverPort)
            {
                verified = true;
            }
        }
        if (verified && (!publicHost || RBX::Network::vitaTcpTunnelEnabled(selected.address.s_addr) || now >= tcpWaitUntil))
        {
            const bool tcp = RBX::Network::vitaTcpTunnelEnabled(selected.address.s_addr);
            std::fprintf(stderr, "Vita server verified: %s gameplay transport\n", tcp ? "TCP" : "UDP");
            if (tcp) RBX::Network::vitaRetainTcpTunnelPreference(selected.address.s_addr);
            RBX::Network::vitaSocketClose(socket);
            return true;
        }
        if(impl->connectionPump)impl->connectionPump();
        sceKernelDelayThread(20000);
    }

    RBX::Network::vitaSocketClose(socket);
    error = "Selected Wi-Fi server stopped responding.";
    return false;
}

void Session::setInstalledPlaceHash(uint64_t hash) { std::lock_guard<std::mutex> lock(impl->mutex); impl->localAdvertisement.placeHash=hash; }

bool Session::selectHost(const DiscoveredHost& selected, std::string& error)
{
    if (selected.advertisement.onDemand) {
        error = "Request a game from this PC before joining.";
        return false;
    }
    DiscoveredHost host;
    bool lanHost = false;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->expireHosts(sceKernelGetSystemTimeWide());
        const auto found = std::find_if(impl->hosts.begin(), impl->hosts.end(),
            [&](const DiscoveredHost& current) {
                return sameAddress(current.address, selected.address) &&
                    current.advertisement.serverPort == selected.advertisement.serverPort &&
                    current.advertisement.placeHash == selected.advertisement.placeHash &&
                    current.advertisement.version == selected.advertisement.version;
            });
        if (found == impl->hosts.end())
        {
            error = "Selected ad hoc host disappeared";
            return false;
        }
        host = *found;
        if (host.advertisement.placeHash != impl->localAdvertisement.placeHash)
        {
            error = "Install this server game before joining.";
            return false;
        }
        lanHost = impl->lanMode;
        if (!lanHost)
        {
            impl->terminalError.clear();
            impl->connectionEstablished = false;
            impl->requestPending = true;
            impl->handshakeAccepted = false;
            impl->connectedPeer = host.address;
            impl->peerAdvertisement = host.advertisement;
            impl->lastStatus = "Requesting Vita ad hoc connection";
        }
    }
    if (lanHost)
    {
        if (!verifyLanHost(host, error))
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->lastStatus = error;
            return false;
        }
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->terminalError.clear();
        impl->connectedPeer = host.address;
        impl->peerAdvertisement = host.advertisement;
        impl->requestPending = false;
        impl->handshakeAccepted = true;
        impl->connectionEstablished = true;
        impl->lastStatus = "Wi-Fi server verified; starting Bloxicon connection";
        return true;
    }
    std::vector<unsigned char> payload = encodeAdvertisement(impl->localAdvertisement);
    const int result = sceNetAdhocMatchingSelectTarget(
        impl->matchingId, &host.address, static_cast<int>(payload.size()), payload.data());
    if (result < 0)
    {
        error = formatError("sceNetAdhocMatchingSelectTarget", result);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->requestPending = false;
        impl->terminalError = error;
        return false;
    }
    return true;
}

bool Session::waitForConnection(unsigned timeoutMs, std::string& error)
{
    const uint64_t deadline = sceKernelGetSystemTimeWide() + uint64_t(timeoutMs) * 1000;
    while (sceKernelGetSystemTimeWide() < deadline)
    {
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->connectionEstablished)
                return true;
            if (!impl->terminalError.empty())
            {
                error = impl->terminalError;
                return false;
            }
        }
        sceKernelDelayThread(20000);
    }
    error = "Timed out waiting for the nearby Vita";
    return false;
}

bool Session::connected() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->connectionEstablished;
}

std::string Session::localAddress() const
{
    if (impl->lanMode)
    {
        SceNetCtlInfo info = {};
        return sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0
            ? std::string(info.ip_address) : std::string();
    }
    SceNetInAddr address = {};
    if (sceNetCtlAdhocGetInAddr(&address) < 0)
        return std::string();
    char text[24] = {};
    return sceNetInetNtop(SCE_NET_AF_INET, &address, text, sizeof(text)) ? text : std::string();
}

std::string Session::connectedAddress() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->connectionEstablished)
        return std::string();
    char text[24] = {};
    return sceNetInetNtop(SCE_NET_AF_INET, &impl->connectedPeer, text, sizeof(text)) ? text : std::string();
}

Advertisement Session::connectedAdvertisement() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->peerAdvertisement;
}

bool Session::usesNativeAdhocTransport() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->transport == Impl::TransportNativeAdhoc;
}

bool Session::usesLanTransport() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->transport == Impl::TransportLan;
}

std::string Session::status() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->lastStatus;
}

std::string Session::failure() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->terminalError;
}

void Session::stop()
{
    if (!impl)
        return;
    if (impl->ownsConnectionDialog)
    {
        sceNetCheckDialogAbort();
        sceNetCheckDialogTerm();
        impl->ownsConnectionDialog = false;
    }
    endFileBatch();
    impl->sharing=false;
    if(impl->shareSocket>=0) sceNetSocketAbort(impl->shareSocket,0);
    if(impl->shareThread.joinable()) impl->shareThread.join();
    if(impl->shareSocket>=0) { RBX::Network::vitaSocketClose(impl->shareSocket); impl->shareSocket=-1; }
    impl->lanRunning = false;
    {
        std::lock_guard<std::mutex> lock(impl->resolverMutex);
        if(impl->publicResolver>=0)sceNetResolverAbort(impl->publicResolver,0);
    }
    if (impl->lanSocket >= 0)
    {
        // Vita's recvfrom can remain asleep despite MSG_DONTWAIT. Abort the
        // socket before joining so Back and app shutdown cannot hang here.
        sceNetSocketAbort(impl->lanSocket, 0);
    }
    if (impl->lanThread.joinable()) impl->lanThread.join();
    if (impl->lanSocket >= 0)
    {
        RBX::Network::vitaSocketClose(impl->lanSocket);
        impl->lanSocket = -1;
    }
    impl->lanMode = false;
    {
        std::unique_lock<std::mutex> lock(callbackOwnerMutex);
        if (callbackOwner == this)
        {
            callbackOwnerStopping = true;
            callbackOwnerIdle.wait(lock, [] { return callbacksInFlight == 0; });
        }
    }
    if (impl->matchingId >= 0)
    {
        sceNetAdhocMatchingStop(impl->matchingId);
        sceNetAdhocMatchingDelete(impl->matchingId);
        impl->matchingId = -1;
    }
    if (impl->matchingInitialized && impl->ownsMatching)
    {
        sceNetAdhocMatchingTerm();
    }
    impl->matchingInitialized = false;
    impl->ownsMatching = false;
    if (impl->pool)
    {
        free(impl->pool);
        impl->pool = nullptr;
    }
    // Keep loaded sysmodules resident for the process lifetime. Their load API
    // does not tell us whether this process or another subsystem owned them.
    impl->moduleLoaded = false;
    if (impl->ownsNativeConnection) sceNetCtlAdhocDisconnect();
    impl->ownsNativeConnection = false;
    if (impl->ownsAdhocCtl)
        sceNetAdhocctlTerm();
    impl->ownsAdhocCtl = false;
    if (impl->ownsAdhoc)
        sceNetAdhocTerm();
    impl->ownsAdhoc = false;
    impl->pspnetModuleLoaded = false;
    {
        std::lock_guard<std::mutex> lock(callbackOwnerMutex);
        if (callbackOwner == this)
        {
            callbackOwner = nullptr;
            callbackOwnerStopping = false;
        }
    }
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->hosts.clear();
    impl->connectionEstablished = false;
    impl->requestPending = false;
    impl->handshakeAccepted = false;
    impl->acceptedPeers.clear();
    impl->connectedPeers.clear();
    impl->terminalError.clear();
    impl->transport = Impl::TransportNone;
    impl->transportPrepared = false;
    impl->hosting = false;
    impl->hostAdvertisementPublished = false;
    impl->connectedPeer = {};
    impl->peerAdvertisement = Advertisement();
    impl->lastStatus = "Ad hoc idle";
}

void Session::matchingCallback(int id, int event, SceNetInAddr* addr, SceSize optLen, void* opt)
{
    Session* owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(callbackOwnerMutex);
        if (!callbackOwnerStopping && callbackOwner)
        {
            owner = callbackOwner;
            ++callbacksInFlight;
        }
    }
    if (owner)
    {
        // Never let an allocation/SDK callback exception escape the C ABI.
        try { owner->onMatchingEvent(id, event, addr, optLen, opt); }
        catch (...) {}
        std::lock_guard<std::mutex> lock(callbackOwnerMutex);
        if (--callbacksInFlight == 0) callbackOwnerIdle.notify_all();
    }
}

void Session::onMatchingEvent(int id, int event, const SceNetInAddr* addr, SceSize optLen, const void* opt)
{
    if (id != impl->matchingId)
        return;
    if (!addr)
    {
        if (event == MatchingError)
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->terminalError = impl->lastStatus = "Vita ad hoc matching error";
        }
        return;
    }

    Advertisement remote;
    const bool validAdvertisement = decodeAdvertisement(opt, optLen, remote);

    if (event == MatchingHello && !impl->hosting && validAdvertisement)
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->rememberHost(*addr, remote, sceKernelGetSystemTimeWide());
        impl->lastStatus = "Nearby Bloxicon Vita host found";
        return;
    }

    if (event == MatchingJoin && impl->hosting)
    {
        Advertisement local;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            local = impl->localAdvertisement;
        }
        if (!validAdvertisement || remote.placeHash != local.placeHash)
        {
            sceNetAdhocMatchingCancelTarget(impl->matchingId,
                const_cast<SceNetInAddr*>(addr));
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->lastStatus = "Rejected peer with a different place build";
            return;
        }
        bool full = false;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (std::any_of(impl->acceptedPeers.begin(), impl->acceptedPeers.end(),
                    [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); }))
                return;
            full = impl->acceptedPeers.size() >= kMaxPeers - 1;
            if (!full)
            {
                impl->acceptedPeers.push_back(*addr);
                impl->peerAdvertisement = remote;
                impl->lastStatus = "Nearby Vita requested to join";
            }
        }
        if (full)
        {
            sceNetAdhocMatchingCancelTarget(impl->matchingId,
                const_cast<SceNetInAddr*>(addr));
            return;
        }
        std::vector<unsigned char> payload = encodeAdvertisement(local);
        const int selected = sceNetAdhocMatchingSelectTarget(
            impl->matchingId, const_cast<SceNetInAddr*>(addr),
            static_cast<int>(payload.size()), payload.data());
        if (selected < 0)
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->acceptedPeers.erase(std::remove_if(impl->acceptedPeers.begin(), impl->acceptedPeers.end(),
                [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); }), impl->acceptedPeers.end());
            impl->lastStatus = formatError("Accept nearby Vita", selected);
        }
        return;
    }

    if (event == MatchingAccept && !impl->hosting)
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (!impl->requestPending || !sameAddress(impl->connectedPeer, *addr)) return;
        if (!validAdvertisement || remote.placeHash != impl->localAdvertisement.placeHash ||
            remote.serverPort != impl->peerAdvertisement.serverPort)
        {
            impl->terminalError = impl->lastStatus = "Selected server changed during connection.";
            impl->requestPending = false;
            return;
        }
        impl->connectedPeer = *addr;
        impl->handshakeAccepted = true;
        impl->peerAdvertisement = remote;
        impl->lastStatus = "Vita ad hoc join accepted";
        return;
    }

    // Matching COMPLETE/ESTABLISHED carries no option payload on the PSP/Vita
    // implementation. Keep metadata from JOIN/ACCEPT and trust only the same
    // callback address that completed the handshake.
    if (event == MatchingComplete)
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (impl->hosting)
        {
            if (std::none_of(impl->acceptedPeers.begin(), impl->acceptedPeers.end(),
                    [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); })) return;
            if (std::none_of(impl->connectedPeers.begin(), impl->connectedPeers.end(),
                    [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); }))
                impl->connectedPeers.push_back(*addr);
        }
        else if (!impl->requestPending || !impl->handshakeAccepted ||
                 !sameAddress(impl->connectedPeer, *addr)) return;
        impl->connectedPeer = *addr;
        impl->connectionEstablished = true;
        impl->lastStatus = "Vita ad hoc peer connected";
        return;
    }

    if (event == MatchingLeft || event == MatchingCancel || event == MatchingTimeout ||
        event == MatchingDisconnect || event == MatchingDataTimeout)
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->hosts.erase(std::remove_if(impl->hosts.begin(), impl->hosts.end(),
            [&](const DiscoveredHost& host) { return sameAddress(host.address, *addr); }), impl->hosts.end());
        if (impl->hosting)
        {
            for (auto* peers : {&impl->acceptedPeers, &impl->connectedPeers})
                peers->erase(std::remove_if(peers->begin(), peers->end(),
                    [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); }), peers->end());
            impl->connectionEstablished = !impl->connectedPeers.empty();
            impl->connectedPeer = impl->connectedPeers.empty() ? SceNetInAddr{} : impl->connectedPeers.front();
        }
        else if (sameAddress(impl->connectedPeer, *addr))
        {
            impl->connectionEstablished = false;
            impl->requestPending = false;
            impl->handshakeAccepted = false;
            impl->terminalError = event == MatchingTimeout ? "Vita ad hoc peer timed out" : "Vita ad hoc peer disconnected";
        }
        impl->lastStatus = event == MatchingTimeout ? "Vita ad hoc peer timed out" : "Vita ad hoc peer disconnected";
        return;
    }

    if (event == MatchingReject || event == MatchingError)
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (impl->hosting)
        {
            for (auto* peers : {&impl->acceptedPeers, &impl->connectedPeers})
                peers->erase(std::remove_if(peers->begin(), peers->end(),
                    [&](const SceNetInAddr& peer) { return sameAddress(peer, *addr); }), peers->end());
            impl->connectionEstablished = !impl->connectedPeers.empty();
            impl->connectedPeer = impl->connectedPeers.empty() ? SceNetInAddr{} : impl->connectedPeers.front();
            impl->lastStatus = "Nearby Vita join failed";
            return;
        }
        if (!sameAddress(impl->connectedPeer, *addr)) return;
        impl->lastStatus = event == MatchingReject ? "Vita ad hoc join rejected" : "Vita ad hoc matching error";
        impl->terminalError = impl->lastStatus;
        impl->requestPending = false;
        impl->connectionEstablished = false;
    }
}

} // namespace VitaAdhoc
#endif
