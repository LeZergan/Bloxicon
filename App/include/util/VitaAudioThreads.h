#pragma once
#include <atomic>
#include <cstdint>
namespace RBX {
inline std::atomic<bool>& vitaAudioCpu3Enabled() { static std::atomic<bool> enabled(false); return enabled; }
inline std::atomic<bool>& vitaSlowJobLogging() { static std::atomic<bool> enabled(false); return enabled; }
inline std::atomic<uint64_t>& vitaAudioMaxMixMicros(){static std::atomic<uint64_t> value(0);return value;}
inline std::atomic<unsigned>& vitaAudioMixOverruns(){static std::atomic<unsigned> value(0);return value;}
inline std::atomic<uint64_t>& vitaAudioMaxBlockGapMicros(){static std::atomic<uint64_t> value(0);return value;}
inline std::atomic<unsigned>& vitaAudioLateBlocks(){static std::atomic<unsigned> value(0);return value;}
inline std::atomic<unsigned>& vitaAudioDrainedBlocks(){static std::atomic<unsigned> value(0);return value;}
}
