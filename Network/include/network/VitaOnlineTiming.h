#pragma once
#include <cstdint>

namespace RBX { namespace Network { namespace VitaOnlineTiming {
// Keep the UI cancellable throughout these budgets. Public prejoin and send
// retries must allow the transport's complete handshake window plus a reply.
static constexpr uint64_t standardPortProbeUs=5000000ULL;
static constexpr uint64_t webSocketProbeUs=8000000ULL;
static constexpr uint64_t legacyHandshakeUs=15000000ULL;
static constexpr uint64_t handshakeUs=standardPortProbeUs+webSocketProbeUs+legacyHandshakeUs;
static constexpr uint64_t publicPrejoinUs=handshakeUs+5000000ULL;
static constexpr uint64_t transientSendUs=handshakeUs+5000000ULL;
// RakNet sends attemptCount+1 requests, then fails on the next interval.
// Leave the whole existing TLS budget plus five seconds for offline replies.
static constexpr unsigned gameplayRetryIntervalMs=1000;
static constexpr unsigned gameplayRetryAttempts=unsigned(publicPrejoinUs/(gameplayRetryIntervalMs*1000ULL))-1;
static_assert((gameplayRetryAttempts+1)*gameplayRetryIntervalMs*1000ULL==publicPrejoinUs,
    "Keep the native retry window aligned with public prejoin and join cancellation");
} } }
