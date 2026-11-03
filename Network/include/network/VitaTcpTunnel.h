#pragma once

#if defined(RBX_PLATFORM_PSVITA) || defined(__vita__)
#include <string>
#include <cstdint>
namespace RBX { namespace Network {
// Prepare certificate parsing on the account worker, then publish memory-only
// credentials. They may only be sent to this exact configured site hostname.
void vitaPrepareOnlineTransport();
void vitaSetOnlineSession(const std::string& host, unsigned short port, const std::string& access, uint64_t until);
void vitaClearOnlineSession();
bool vitaOnlineService(std::string& host,unsigned short& port);
bool vitaConfigureAccountTunnel(unsigned int address,unsigned short port,const std::string& hostname);
// SDK-compatible datagram boundary. Public endpoints switch only after a
// framed TCP response; nearby Vita and ordinary LAN traffic keep using UDP.
int vitaSendto(int socket, const void* data, unsigned size, int flags, const void* address, unsigned addressSize);
int vitaRecvfrom(int socket, void* data, unsigned size, int flags, void* address, unsigned* addressSize);
int vitaSocketClose(int socket);
int vitaPosixSendto(int socket, const void* data, unsigned size, int flags, const void* address, unsigned addressSize);
int vitaPosixRecvfrom(int socket, void* data, unsigned size, int flags, void* address, unsigned* addressSize);
int vitaPosixSocketClose(int socket);
void vitaProbeTcpTunnel(int socket, const void* address, unsigned addressSize, const void* data, unsigned size);
bool vitaTcpTunnelEnabled(unsigned int address);
// True only for an account-bound required-TCP route with current credentials
// and a verified live response or retained prejoin handoff, never plain LAN.
bool vitaVerifiedAccountTcpRoute(unsigned int address);
// Configure the raw TCP ingress independently from framed discovery/game ports.
// A host IP has one ingress, matching the legacy protocol's peer identity.
// Required TCP routes never contact an older UDP service on the same address.
bool vitaConfigureTcpTunnel(unsigned int address, unsigned short port, bool requireTcp = false);
// Carry a verified prejoin choice across closing its temporary probe socket.
// The bounded lease expires if no gameplay socket uses it.
void vitaRetainTcpTunnelPreference(unsigned int address);
// A fresh RakNet join is required: replacing a tunnel changes the server's
// UDP peer identity and must not pretend to resume the old game session.
bool vitaTcpTunnelFailed(unsigned int address, unsigned short port);
} }
#endif
