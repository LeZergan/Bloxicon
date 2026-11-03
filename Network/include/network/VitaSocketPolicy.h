#pragma once

#ifdef RBX_PLATFORM_PSVITA

namespace RBX {
namespace Network {

// RakNet's SocketDescriptor::extraSocketOptions is unused on Vita. Reserve one
// high bit as an internal request for Sony's UDP_P2P socket type; the bit is
// stripped before the remaining value is passed as the protocol argument.
static const unsigned int kVitaRakNetP2PSocketOption = 0x80000000u;

// Sony/Vita ad hoc P2P datagrams use the NP physical port plus a virtual port.
// 3658 is SCE_NET_ADHOC_PORT/SCE_NP_PORT in the retail Vita networking stack.
static const unsigned short kVitaAdhocPhysicalPort = 3658;
static const unsigned short kVitaRakNetClientVPort = 53641;

void setVitaAdhocP2PTransport(bool enabled);
bool vitaAdhocP2PTransportEnabled();
unsigned int vitaRakNetSocketOptions();

} // namespace Network
} // namespace RBX

#endif
