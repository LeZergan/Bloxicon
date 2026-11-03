#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include "rbx/VitaJobMonitor.h"

// Independent evidence when the render thread cannot reach its exception handler.
// This never interrupts a job, times out gameplay, or restarts the process.
class VitaFrameMonitor
{
    std::atomic<const char*> phase;
    std::atomic<uint32_t> heartbeat;
    std::atomic<unsigned> frame;
    std::atomic<bool> active, running;
    SceUID thread, renderThread;

    static uint32_t milliseconds()
    {
        return uint32_t(sceKernelGetSystemTimeWide() / 1000);
    }

    void writeEvidence(const char* event, uint32_t elapsed, bool snapshot = false, int startResult = 0)
    {
        SceKernelThreadInfo info = {};
        info.size = sizeof(info);
        const int result = sceKernelGetThreadInfo(renderThread, &info);
        char line[384];
        const int size = std::snprintf(line, sizeof(line),
            "event=%s time_ms=%u frame=%u idle_ms=%u phase=%s thread=%d monitor=%d start_result=%d info_result=%d status=%u wait_type=%u wait_id=%d cpu=%d\n",
            event, milliseconds(), frame.load(), elapsed, phase.load(),
            renderThread, thread, startResult, result, info.status, info.waitType, info.waitId, info.currentCpuId);
        // Native I/O avoids stderr's lock and heap allocation on a hung thread.
        SceUID file = sceIoOpen(snapshot ? "ux0:/data/RBXV00001/system/runtime-heartbeat.txt" :
            "ux0:/data/RBXV00001/system/runtime-stall.txt",
            SCE_O_WRONLY | SCE_O_CREAT | (snapshot ? SCE_O_TRUNC : SCE_O_APPEND), 0666);
        if (file >= 0)
        {
            if (size > 0 && size < int(sizeof(line))) sceIoWrite(file, line, size);
            if (elapsed >= 15000) {
                for (unsigned i = 0; i < 16; ++i) {
                    RBX::VitaJobMonitorSnapshot job;
                    if (!RBX::vitaReadJobMonitor(RBX::vitaJobMonitorSlots()[i], job)) continue;
                    SceKernelThreadInfo worker = {};
                    worker.size = sizeof(worker);
                    const int workerResult = sceKernelGetThreadInfo(job.thread, &worker);
                    const int length = std::snprintf(line, sizeof(line),
                        "job_thread=%d generation=%u age_ms=%u name=%s phase=%s info_result=%d status=%u wait_type=%u wait_id=%d cpu=%d\n",
                        job.thread, job.generation, milliseconds() - job.startedMs, job.name,
                        job.phase, workerResult, worker.status, worker.waitType, worker.waitId, worker.currentCpuId);
                    if (length > 0 && length < int(sizeof(line))) sceIoWrite(file, line, length);
                }
            }
            sceIoClose(file);
        }
    }

    static int monitorThread(SceSize, void* args)
    {
        VitaFrameMonitor& self = **static_cast<VitaFrameMonitor**>(args);
        bool reported = false;
        unsigned records = 0;
        self.writeEvidence("monitor_started", 0);
        uint32_t lastSnapshot = milliseconds();
        while (self.running.load())
        {
            if (self.active.load())
            {
                const uint32_t lastFrame = self.heartbeat.load();
                const uint32_t elapsed = milliseconds() - lastFrame;
                const bool stalled = elapsed >= 15000;
                const uint32_t now = milliseconds();
                if (now - lastSnapshot >= 5000)
                {
                    self.writeEvidence("monitor_heartbeat", elapsed, true);
                    lastSnapshot = now;
                }
                if (stalled != reported && records < 32)
                {
                    self.writeEvidence(stalled ? "frame_stall_candidate" : "frames_resumed", elapsed);
                    ++records;
                }
                reported = stalled;
            }
            sceKernelDelayThread(250000);
        }
        return 0;
    }

public:
    explicit VitaFrameMonitor(const char* initialPhase)
        : phase(initialPhase), heartbeat(0), frame(0), active(false), running(true),
          thread(-1), renderThread(sceKernelGetThreadId())
    {
        SceUID file = sceIoOpen("ux0:/data/RBXV00001/system/runtime-stall.txt",
            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (file >= 0) sceIoClose(file);
        writeEvidence("monitor_initializing", 0, true);
        thread = sceKernelCreateThread("rbx_frame_monitor", monitorThread, 0x10000100,
            0x4000, 0, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
        if (thread >= 0)
        {
            VitaFrameMonitor* self = this;
            const int result = sceKernelStartThread(thread, sizeof(self), &self);
            if (result < 0)
            {
                writeEvidence("monitor_start_failed", 0, false, result);
                sceKernelDeleteThread(thread);
                thread = -1;
            }
        }
        else
            writeEvidence("monitor_create_failed", 0, false, thread);
    }

    ~VitaFrameMonitor()
    {
        running.store(false);
        if (thread >= 0)
        {
            sceKernelWaitThreadEnd(thread, NULL, NULL);
            sceKernelDeleteThread(thread);
        }
    }

    void beginFrame(unsigned number)
    {
        frame.store(number);
        heartbeat.store(milliseconds());
        active.store(true);
    }

    VitaFrameMonitor& operator=(const char* nextPhase)
    {
        phase.store(nextPhase);
        return *this;
    }

    operator const char*() const { return phase.load(); }
};
