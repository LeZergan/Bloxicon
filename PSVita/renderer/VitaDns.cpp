#include <netdb.h>
#include <cstring>
#include <mutex>

extern "C" struct hostent* __real_gethostbyname(const char* name);

// Vita newlib returns one process-wide hostent and IPv4 address. Concurrent
// curl/engine lookups otherwise overwrite each other's destination before
// getaddrinfo can copy it. Keep the SDK call serialized, then return a complete
// per-thread copy; no HTTP transfer or TLS work holds this gate.
extern "C" struct hostent* __wrap_gethostbyname(const char* name)
{
    static std::mutex gate;
    struct Record {
        hostent entry{};
        char name[512]{};
        unsigned char address[4]{};
        char* addresses[2]{};
    };
    static thread_local Record copy;
    std::lock_guard<std::mutex> lock(gate);
    hostent* resolved = __real_gethostbyname(name);
    if (!resolved) return nullptr;
    if (resolved->h_length != 4 || !resolved->h_addr_list || !resolved->h_addr_list[0])
        return nullptr;
    copy.entry = *resolved;
    std::strncpy(copy.name, resolved->h_name ? resolved->h_name : "", sizeof(copy.name)-1);
    copy.name[sizeof(copy.name)-1] = 0;
    std::memcpy(copy.address, resolved->h_addr_list[0], sizeof(copy.address));
    copy.addresses[0] = reinterpret_cast<char*>(copy.address);
    copy.addresses[1] = nullptr;
    copy.entry.h_name = copy.name;
    copy.entry.h_aliases = nullptr;
    copy.entry.h_addr_list = copy.addresses;
    return &copy.entry;
}
