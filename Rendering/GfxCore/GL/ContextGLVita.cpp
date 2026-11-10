// SPDX-License-Identifier: GPL-3.0-only
#include "ContextGL.h"

#include <vitaGL.h>
#include <psp2/kernel/sysmem.h>
#include <chrono>
#include <cstdio>

// The SDK vitaGL archive starts its animated boot logo from vglInitExtended.
// Keep the verified graphics archive while using the game's own loading view.
extern "C" void __wrap_invoke_splashscreen(void) {}
extern "C" void vita_loader_first_frame(void) __attribute__((weak));

namespace RBX
{
namespace Graphics
{

namespace
{
const unsigned int kVitaWidth = 960;
const unsigned int kVitaHeight = 544;
// Pair the 208 MiB engine heap with a 32 MiB native driver/kernel reserve.
// Their sum matches the previous 224+16 MiB split, keeping the mapped graphics
// pool budget while allowing GXM to allocate additional framebuffer targets.
const int kVitaGLRamThreshold = 32 * 1024 * 1024;
const int kVitaGLPhycontReserve = 4 * 1024 * 1024;
// The pinned driver rounds its PhyCont pool UP to 1 MiB. Compensate in the
// threshold so its own free-memory sample leaves at least the reserve:
// ceil(max(free - (reserve + alignment - 1), 0) / alignment) * alignment.
// If less than reserve + 1 MiB is available, it requests no PhyCont pool.
const int kVitaGLPhycontThreshold = kVitaGLPhycontReserve + 1024 * 1024 - 1;
// Preserve vglInitExtended's common-dialog policy from the pinned driver;
// this is its private SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE constant.
const int kVitaGLCommonDialogThreshold = 0x8c6000;
}

class ContextGLVita: public ContextGL
{
public:
    explicit ContextGLVita(void* windowHandle)
    {
        (void)windowHandle;
        const auto graphicsStart=std::chrono::steady_clock::now();

        // vitaGL returns whether it had to fall back to another resolution,
        // not whether initialization succeeded. 960x544 normally returns
        // GL_FALSE here, so do not treat the return value as an error.
        // The native level browser uses vitaGL's immediate 2D pipeline. A
        // zero-sized legacy pool makes its first glVertex call write to null.
        const GLboolean resolutionFallback = vglInitWithCustomThreshold(512 * 1024,
            kVitaWidth, kVitaHeight, kVitaGLRamThreshold, 0,
            kVitaGLPhycontThreshold, kVitaGLCommonDialogThreshold, SCE_GXM_MULTISAMPLE_NONE);
        const auto graphicsReady=std::chrono::steady_clock::now();
        SceKernelFreeMemorySizeInfo memory = {};
        memory.size = sizeof(memory);
        const int memoryResult = sceKernelGetFreeMemorySize(&memory);
        const int reserveMet = memoryResult < 0 || memory.size_phycont < 0 ? -1 :
            (memory.size_phycont >= kVitaGLPhycontReserve ? 1 : 0);
        std::fprintf(stderr,"Vita Boot graphics initialization %.2f ms fallback %u memrc 0x%08x free_user %d free_cdram %d free_phycont %d phycont_reserve %d met %d\n",
            std::chrono::duration<double,std::milli>(graphicsReady-graphicsStart).count(),
            unsigned(resolutionFallback), unsigned(memoryResult),
            memoryResult >= 0 ? memory.size_user : -1,
            memoryResult >= 0 ? memory.size_cdram : -1,
            memoryResult >= 0 ? memory.size_phycont : -1,
            kVitaGLPhycontReserve, reserveMet);
        // The CPU boot image remains on the display until the first complete
        // loader card is ready. Do not swap blank clear frames over it.
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,kVitaWidth,kVitaHeight);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(242.f/255.f,242.f/255.f,242.f/255.f,1.f);
        if(vita_loader_first_frame)
            vita_loader_first_frame();
        else {
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_FALSE);
        }
        std::fprintf(stderr,"Vita Boot first graphics card %.2f ms\n",
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-graphicsReady).count());
    }

    virtual void setCurrent()
    {
        // vitaGL owns a single process-wide graphics context on Vita.
    }

    virtual void swapBuffers()
    {
        // Composite native dialogs (text entry) over the game's framebuffer.
        vglSwapBuffers(GL_TRUE);
    }

    virtual unsigned int getMainFramebufferId()
    {
        return 0;
    }

    virtual bool isMainFramebufferRetina()
    {
        return false;
    }

    virtual std::pair<unsigned int, unsigned int> updateMainFramebuffer(unsigned int width, unsigned int height)
    {
        (void)width;
        (void)height;
        return std::make_pair(kVitaWidth, kVitaHeight);
    }
};

ContextGL* ContextGL::create(void* windowHandle)
{
    return new ContextGLVita(windowHandle);
}

void ContextGL::setVitaSwapPacing(bool gameplay, bool vsync, unsigned fpsCap)
{
    // 20/30/60 divide the 60 Hz display into three/two/one vblanks.
    // Legacy caps are normalized by the shared preferences reader.
    // Loader pages always use the display clock; disabling gameplay VSync
    // must not make menu loops busy-spin or overload the audio mixer.
    eglSwapInterval(NULL, (!gameplay || vsync) ? (gameplay ? (fpsCap==20?3:fpsCap==30?2:1) : 1) : 0);
}

}
}
