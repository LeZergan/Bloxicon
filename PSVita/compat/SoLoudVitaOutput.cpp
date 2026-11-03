/*
SoLoud Vita homebrew output backend
Copyright (c) 2017 Ilya Zhuravlev

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/


// Altered for Bloxicon Vita: main audio port, checked startup and output,
// constructed atomics, bounded recovery, and a receipt for nonzero PCM output.
#include <soloud.h>
#include <atomic>
#include <memory>
#include <new>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "util/VitaAudioThreads.h"
#include "util/VitaWorkerAffinity.h"
namespace SoLoud {
struct VitaData {
 std::atomic<bool> done{false};int port=-1,tid=-1;unsigned samples=0;
 Soloud* soloud=nullptr;std::unique_ptr<short[]> buffers[2];
};
static int openOutput(unsigned samples) {
 return sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,samples,48000,SCE_AUDIO_OUT_MODE_STEREO);
}
static void vitaCleanup(Soloud* engine) {
 auto* data=static_cast<VitaData*>(engine->mBackendData);if(!data)return;
 data->done.store(true,std::memory_order_release);
 if(data->tid>=0){sceKernelWaitThreadEnd(data->tid,nullptr,nullptr);sceKernelDeleteThread(data->tid);}
 if(data->port>=0)sceAudioOutReleasePort(data->port);
 delete data;engine->mBackendData=nullptr;
}
static int vitaOutput(SceSize,void* args) {
 auto* data=*static_cast<VitaData**>(args);unsigned slot=0,blocks=0,retries=0;bool reported=false;
 // Decoding time alone misses starvation before the mixer gets scheduled.
 // Request a checked real-time user priority after the known-working thread
 // startup. Unsupported firmware keeps the existing priority and output.
 const int priorityResult=sceKernelChangeThreadPriority(sceKernelGetThreadId(),64);
 std::fprintf(stderr,"Vita audio output priority: requested=64 result=0x%08x\n",unsigned(priorityResult));
 uint64_t previousMix=0;
 const uint64_t period=uint64_t(data->samples)*1000000/48000;
 RBX::VitaWorkerAffinity affinity;
 const SceUID current=sceKernelGetThreadId();
 while(!data->done.load(std::memory_order_acquire)) {
  const bool cpu3=RBX::vitaAudioCpu3Enabled().load(std::memory_order_acquire);
  affinity.configure(current,cpu3);
  auto* pcm=data->buffers[slot].get();
  const uint64_t mixStart=sceKernelGetProcessTimeWide();
  if(previousMix){
   const uint64_t gap=mixStart-previousMix;
   auto& maximum=RBX::vitaAudioMaxBlockGapMicros();uint64_t prior=maximum.load(std::memory_order_relaxed);
   while(prior<gap&&!maximum.compare_exchange_weak(prior,gap,std::memory_order_relaxed)){}
   if(gap>period+2000)RBX::vitaAudioLateBlocks().fetch_add(1,std::memory_order_relaxed);
   if(blocks && data->port>=0 && sceAudioOutGetRestSample(data->port)==0)RBX::vitaAudioDrainedBlocks().fetch_add(1,std::memory_order_relaxed);
  }
  previousMix=mixStart;
  data->soloud->mixSigned16(pcm,data->samples);
  const uint64_t mixEnd=sceKernelGetProcessTimeWide();
  const uint64_t duration=mixEnd-mixStart;
  auto& maximum=RBX::vitaAudioMaxMixMicros();uint64_t previous=maximum.load(std::memory_order_relaxed);
  while(previous<duration&&!maximum.compare_exchange_weak(previous,duration,std::memory_order_relaxed)){}
  if(duration>uint64_t(data->samples)*1000000/48000)RBX::vitaAudioMixOverruns().fetch_add(1,std::memory_order_relaxed);
  const int result=sceAudioOutOutput(data->port,pcm);
  if(result<0) {
   previousMix=0;
   if(retries++<3)std::fprintf(stderr,"Vita audio output failed: 0x%08x; reopening main port\n",unsigned(result));
   sceAudioOutReleasePort(data->port);data->port=-1;
   // A sleeping failed output must not spin at mixer speed or starve loading.
   sceKernelDelayThread(100000);data->port=openOutput(data->samples);
   if(data->port<0){sceKernelDelayThread(400000);continue;}
  } else {
   ++blocks;
   if(!reported) {
    int peak=0;for(unsigned i=0;i<data->samples*2;++i)peak=std::max(peak,std::abs(int(pcm[i])));
    if(peak){reported=true;std::fprintf(stderr,"Vita audio PCM delivered: port=main rate=48000 blocks=%u peak=%d\n",blocks,peak);}
   }
  }
  slot^=1;
 }
 return 0;
}
result vita_homebrew_init(Soloud* engine,unsigned flags,unsigned rate,unsigned samples,unsigned channels) {
 if(rate!=48000 || channels!=2 || samples<64 || samples>65472 || samples%64)return INVALID_PARAMETER;
 std::unique_ptr<VitaData> data(new(std::nothrow) VitaData);
 if(!data)return OUT_OF_MEMORY;
 for(auto& buffer:data->buffers){buffer.reset(new(std::nothrow) short[samples*2]);if(!buffer)return OUT_OF_MEMORY;}
 data->port=openOutput(samples);
 if(data->port<0){std::fprintf(stderr,"Vita audio main port failed: 0x%08x\n",unsigned(data->port));return UNKNOWN_ERROR;}
 data->samples=samples;data->soloud=engine;
 engine->mBackendData=data.release();engine->mBackendCleanupFunc=vitaCleanup;
 try {engine->postinit(rate,samples,flags,channels);}catch(...){vitaCleanup(engine);return OUT_OF_MEMORY;}
 auto* state=static_cast<VitaData*>(engine->mBackendData);
 // Use the retail-supported priority already verified by this backend.
 // The lower-priority PCM decoder cannot take precedence over output.
 state->tid=sceKernelCreateThread("RBX audio output",vitaOutput,0x10000100,0x10000,0,SCE_KERNEL_CPU_MASK_USER_ALL,nullptr);
 if(state->tid<0){std::fprintf(stderr,"Vita audio thread create failed: 0x%08x\n",unsigned(state->tid));vitaCleanup(engine);return UNKNOWN_ERROR;}
 const int result=sceKernelStartThread(state->tid,sizeof(state),&state);
 if(result<0){std::fprintf(stderr,"Vita audio thread start failed: 0x%08x\n",unsigned(result));sceKernelDeleteThread(state->tid);state->tid=-1;vitaCleanup(engine);return UNKNOWN_ERROR;}
 return SO_NO_ERROR;
}
}
