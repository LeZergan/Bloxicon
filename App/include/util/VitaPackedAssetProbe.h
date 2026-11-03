#pragma once

// The archive reader is optional in small Vita probes. Fall back to the
// original readable-file check when this executable has no mounted reader.
extern "C" bool vita_runtime_pack_has_file(const char*) __attribute__((weak));

namespace RBX {
inline bool vitaPackedAssetExists(const char* path)
{
    return vita_runtime_pack_has_file && vita_runtime_pack_has_file(path);
}
}
