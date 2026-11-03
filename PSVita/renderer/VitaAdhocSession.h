#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifdef __vita__
#include <psp2/types.h>
#include <psp2/net/net.h>
#endif

namespace VitaAdhoc
{

static const uint32_t kAdvertisementMagic = 0x52425856u; // "RBXV"
static const uint16_t kAdvertisementVersion = 1;
static const uint32_t kLanControlMagic = 0x5242584cu; // "RBXL"
static const uint16_t kMatchingPort = 31876;
static const uint16_t kRobloxServerPort = 53640;
static const unsigned kMaxPeers = 4;

struct Advertisement
{
    uint16_t version = kAdvertisementVersion;
    uint16_t serverPort = kRobloxServerPort;
    uint64_t placeHash = 0;
    uint64_t placeId = 0;
    std::string hostName;
    std::string placeName;
    bool onDemand = false;
    bool rosterKnown = false;
    std::vector<std::string> playerNames;
    bool quotaKnown = false;
    uint8_t playersUsed = 0, playersLimit = 0;
    uint8_t serversUsed = 0, serversLimit = 0;
    uint8_t totalPlayers = 0, totalCapacity = 0;
};

std::vector<unsigned char> encodeAdvertisement(const Advertisement& value);
bool decodeAdvertisement(const void* bytes, size_t size, Advertisement& value);

enum LanControlType
{
    LanProbe = 1,
    LanAck = 2,
    LanSnapshotEnd = 3
};

struct LanControl
{
    uint8_t type = LanProbe;
    uint8_t features = 0; // bit0: nonce-bound complete server-list snapshot
    uint64_t nonce = 0;
    uint64_t placeHash = 0;
    uint16_t serverPort = 0;
};

std::vector<unsigned char> encodeLanControl(const LanControl& value);
bool decodeLanControl(const void* bytes, size_t size, LanControl& value);

#ifdef __vita__
struct DiscoveredHost
{
    SceNetInAddr address = {};
    Advertisement advertisement;
    std::string sourceProfile; // Saved endpoint identity; never rendered as an address.
    uint64_t lastSeenUs = 0;
    uint32_t pingMs = 0;
    bool pingKnown = false;
    uint64_t pingSampledUs = 0;
};

class Session
{
public:
    explicit Session(std::function<void()> connectionPump = {});
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Host prepares the transport before RakNet binds its gameplay socket. This
    // lets direct Vita ad hoc use UDP_P2P while Wi-Fi fallback keeps normal UDP.
    bool prepareHostTransport(std::string& error, bool allowLanFallback = true);
    bool startHost(const Advertisement& value, std::string& error);
    bool startDiscovery(const Advertisement& value, std::string& error, bool preferLan = false);
    std::vector<DiscoveredHost> discoveredHosts() const;
    void setInstalledPlaceHash(uint64_t hash);
    bool startSharing(const std::string& placePath, uint64_t hash, std::string& error,
                      const std::vector<std::string>& assets = {});
    bool receiveFile(const DiscoveredHost& host, uint32_t file, const std::string& destination,
                     const std::function<bool(uint64_t,uint64_t)>& progress, std::string& error,
                     uint64_t maximumBytes = 48ull*1024*1024);
    // One bounded transfer socket/admission for a verified host and fingerprint.
    bool beginFileBatch(const DiscoveredHost& host, std::string& error,
                        const std::function<bool()>& continuation = {});
    void endFileBatch();
    bool isVerifiedAccountHost(const DiscoveredHost& host) const;
    bool selectHost(const DiscoveredHost& selected, std::string& error);
    bool waitForConnection(unsigned timeoutMs, std::string& error);
    bool connected() const;
    std::string localAddress() const;
    std::string connectedAddress() const;
    Advertisement connectedAdvertisement() const;
    bool usesNativeAdhocTransport() const;
    bool usesLanTransport() const;
    std::string status() const;
    std::string failure() const;
    void updateRoster(const std::vector<std::string>& names);
    void stop();

private:
    bool initialize(std::string& error);
    bool prepareNativeAdhoc(std::string& error);
    bool startLan(const Advertisement& value, bool hosting, std::string& error);
    bool verifyLanHost(const DiscoveredHost& selected, std::string& error, const std::function<bool()>& continuation = {});
    bool createMatcher(int mode, const Advertisement& hello, std::string& error, bool advertise = true);
    static void matchingCallback(int id, int event, SceNetInAddr* addr, SceSize optLen, void* opt);
    void onMatchingEvent(int id, int event, const SceNetInAddr* addr, SceSize optLen, const void* opt);
    static std::string formatError(const char* operation, int code);

    struct Impl;
    Impl* impl;
};
#endif

} // namespace VitaAdhoc
