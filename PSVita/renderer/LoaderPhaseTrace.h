#pragma once

// Native-only observations. Keep the UI, audio and controller behavior intact.
// A retained heartbeat identifies a loader stall without requiring live FTP.
#ifdef __vita__
extern "C" void vita_loader_phase(const char*) __attribute__((weak));
extern "C" void vita_loader_frame() __attribute__((weak));
namespace VitaBrowser {
inline void traceLoaderPhase(const char* phase) {
    if (vita_loader_phase) vita_loader_phase(phase);
}
inline void traceLoaderFrame() {
    if (vita_loader_frame) vita_loader_frame();
}
}
#endif
