#pragma once
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <psp2/io/stat.h>

namespace VitaLoaderDiagnostics {
inline constexpr char directory[]="ux0:/data/Bloxicon/system/logs";
inline std::atomic<bool>& detailed(){static std::atomic<bool> enabled{false};return enabled;}
inline void ensureDirectory(){sceIoMkdir(directory,0777);}
inline void configure(bool requested)
{
    static const bool marker=[]{
        SceIoStat info{};
        for(const char* path:{"ux0:/data/Bloxicon/system/load-audit.enabled",
                "ux0:/data/Bloxicon/system/frame-budget.enabled",
                "ux0:/data/Bloxicon/system/loader-input-trace.enabled"})
            if(sceIoGetstat(path,&info)>=0)return true;
        return false;
    }();
    detailed().store(requested||marker,std::memory_order_relaxed);
    if(detailed().load(std::memory_order_relaxed))ensureDirectory();
}
inline void tidyLegacy()
{
    struct Log {const char* before;const char* after;};
    for(const auto& log:{
        Log{"ux0:/data/Bloxicon/system/loader-state.txt","ux0:/data/Bloxicon/system/logs/legacy-loader-state.txt"},
        Log{"ux0:/data/Bloxicon/system/runtime-heartbeat.txt","ux0:/data/Bloxicon/system/logs/legacy-runtime-heartbeat.txt"},
        Log{"ux0:/data/Bloxicon/system/runtime-stall.txt","ux0:/data/Bloxicon/system/logs/legacy-runtime-stall.txt"},
        Log{"ux0:/data/Bloxicon/system/runtime-memory.txt","ux0:/data/Bloxicon/system/logs/legacy-runtime-memory.txt"},
        Log{"ux0:/data/Bloxicon/system/last-load-report.txt","ux0:/data/Bloxicon/system/logs/legacy-load-report.txt"}}){
        SceIoStat before{},after{};
        if(sceIoGetstat(log.before,&before)>=0&&SCE_S_ISREG(before.st_mode)&&sceIoGetstat(log.after,&after)<0){
            ensureDirectory();
            std::rename(log.before,log.after);
        }
    }
    // Native rmdir only removes an empty directory.
    for(const char* path:{"ux0:/data/Bloxicon/system/cache","ux0:/data/Bloxicon/system/image-cache",
            "ux0:/data/Bloxicon/system/programs","ux0:/data/Bloxicon/system/thumbnails",
            "ux0:/data/Bloxicon/system/place-cache","ux0:/data/Bloxicon/system/local-saves",
            "ux0:/data/Bloxicon/system/avatar","ux0:/data/Bloxicon/system/logs"})
        sceIoRmdir(path);
}
}
