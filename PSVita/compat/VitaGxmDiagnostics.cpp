// Preserve native graphics results to identify failed scene transitions.
// No stdio, heap allocation or pthread locks in the diagnostic path.
#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>

extern "C" int __real_sceGxmBeginScene(SceGxmContext*, unsigned, const SceGxmRenderTarget*, const SceGxmValidRegion*, SceGxmSyncObject*, SceGxmSyncObject*, const SceGxmColorSurface*, const SceGxmDepthStencilSurface*);
extern "C" int __real_sceGxmCreateRenderTarget(const SceGxmRenderTargetParams*, SceGxmRenderTarget**);

namespace {
unsigned transitions = 0, failures = 0;
const SceGxmRenderTarget* lastTarget = nullptr;
void record(const char* operation, int result, const void* context, const void* target, const void* color, const void* depth, unsigned width, unsigned height)
{
    char line[256];
    int n = sceClibSnprintf(line, sizeof(line), "gxm %s result=0x%08x context=%p target=%p color=%p depth=%p size=%ux%u\n", operation, static_cast<unsigned>(result), context, target, color, depth, width, height);
    SceUID file = sceIoOpen("ux0:/data/RBXV00001/system/logs/gxm-resource.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (file >= 0) {
        if (n > 0) sceIoWrite(file, line, n < static_cast<int>(sizeof(line)) ? n : sizeof(line) - 1);
        sceIoClose(file);
    }
}
}
extern "C" int __wrap_sceGxmBeginScene(SceGxmContext* context, unsigned flags, const SceGxmRenderTarget* target, const SceGxmValidRegion* region, SceGxmSyncObject* vertexSync, SceGxmSyncObject* fragmentSync, const SceGxmColorSurface* color, const SceGxmDepthStencilSurface* depth)
{
    int result = __real_sceGxmBeginScene(context, flags, target, region, vertexSync, fragmentSync, color, depth);
    if ((result < 0 && ++failures <= 16) || (target != lastTarget && ++transitions <= 128))
        record("begin", result, context, target, color ? sceGxmColorSurfaceGetData(color) : nullptr, depth, 0, 0);
    lastTarget = target;
    return result;
}
extern "C" void vita_gxm_record_end(SceGxmContext* context, int result)
{
    if (result < 0 && ++failures <= 16) record("end", result, context, nullptr, nullptr, nullptr, 0, 0);
}
extern "C" int __wrap_sceGxmCreateRenderTarget(const SceGxmRenderTargetParams* params, SceGxmRenderTarget** target)
{
    int result = __real_sceGxmCreateRenderTarget(params, target);
    record("create_target", result, nullptr, result == 0 && target ? *target : nullptr, nullptr, nullptr, params ? params->width : 0, params ? params->height : 0);
    return result;
}
