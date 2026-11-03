# Setup

Use an authorized checkout of the baseline engine and apply the Vita source overlay described in [Vita source layout](development.md). Set `VITASDK` and configure the renderer with CMake.

```sh
cmake -S PSVita/renderer -B build-vita -G Ninja -DCMAKE_BUILD_TYPE=Release -DRBX_REUSE_ENGINE_ARCHIVES=OFF
cmake --build build-vita --target roblox2016_vita_scene_vpk
```

Use the matching verified runtime data for any hardware test. Preserve profiles, settings, native controller controls, and all original place bytes. Verify original SHA-256 hashes before and after testing, and report native loading, partial gameplay, and fully tested gameplay separately.
