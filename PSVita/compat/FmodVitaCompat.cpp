#include <fmod.hpp>
#include <fmod.h>
#include <soloud.h>
#include <soloud_wav.h>
#include <soloud_wavstream.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <new>
#include <vector>
#include <fstream>
#include <memory>
#include <chrono>

#define MINIMP3_IMPLEMENTATION
#include "thirdparty/minimp3/minimp3_ex.h"
#include "VitaMp3Stream.h"
#include "util/VitaAudioThreads.h"
#define VITA_AUDIO_MP3_CACHE
#include "VitaShortSoundCache.h"

// Roblox 2016 only uses a small subset of FMOD's low-level API. The original
// Vita library is unavailable, so these opaque FMOD objects adapt that subset
// to SoLoud's native Vita output backend.
// minimp3 is pinned from lieff/minimp3 commit ea99364f61c14656440e8d77e9c233ccf3124633 (CC0-1.0).
namespace {
std::atomic<size_t> audioOwnedBytes(0), audioPeakBytes(0);
std::atomic<float> userGameVolume{1.f};
std::atomic<FMOD::System*> currentSystem{nullptr};
std::atomic<unsigned> audioGeneration{0};
enum ObjectKind { ObjectChannel, ObjectGroup };
struct VitaSystem;
struct VitaControl {
    ObjectKind kind; VitaSystem* system; float volume; bool muted;
    VitaControl(ObjectKind k, VitaSystem* s): kind(k), system(s), volume(1.f), muted(false) {}
};
struct VitaSound {
    VitaSystem* system; std::unique_ptr<SoLoud::WavStream> stream;
    FMOD_MODE mode; bool streaming; bool mp3 = false; VitaAudio::Mp3Stream mp3Stream; size_t ownedBytes = 0;
    std::shared_ptr<VitaAudio::ShortPcm> pcm;
    VitaSound(VitaSystem* s, FMOD_MODE m): system(s), stream(new SoLoud::WavStream), mode(m), streaming(false) {}
    SoLoud::AudioSource& source() { return mp3 ? static_cast<SoLoud::AudioSource&>(mp3Stream) : static_cast<SoLoud::AudioSource&>(*stream); }
    SoLoud::AudioSource& playbackSource() {
        return pcm && pcm->ready.load(std::memory_order_acquire) ?
            static_cast<SoLoud::AudioSource&>(*pcm->wave) : source();
    }
    float sampleRate() const { return mp3 ? mp3Stream.mBaseSamplerate : stream->mBaseSamplerate; }
    double length() { return mp3 ? mp3Stream.getLength() : stream->getLength(); }
};
struct VitaChannel: VitaControl {
    VitaSound* sound; SoLoud::handle handle; FMOD_MODE mode; float frequency;
    int loopLimit = -1; unsigned loopStart = 0;
    bool soundtrack=false, userInterface=false;
    unsigned musicLoopsReported=0;
    int priority; void* userData; FMOD_CHANNELCONTROL_CALLBACK callback; bool reportedEnd;
    VitaChannel(VitaSystem* s, VitaSound* w, SoLoud::handle h)
        : VitaControl(ObjectChannel, s), sound(w), handle(h), mode(w->mode),
          frequency(w->sampleRate()), priority(128), userData(NULL),
          callback(NULL), reportedEnd(false) {}
};
struct VitaGroup: VitaControl { explicit VitaGroup(VitaSystem* s): VitaControl(ObjectGroup, s) {} };
struct VitaSystem {
    VitaAudio::ShortSoundCache shortSounds;
    SoLoud::Soloud engine; unsigned sampleRate; int driver; bool initialized;
    float rolloff = 1.f, doppler = 1.f;
    double updateCpu = 0;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastBudgetReport;
    VitaGroup* master; std::vector<VitaChannel*> channels;
    // The checked native backend uses Vita's main 48 kHz stereo audio port.
    // SoLoud resamples legacy sounds to the device output frequency.
    VitaSystem(): sampleRate(48000), driver(0), initialized(false), master(NULL) {}
};

template<class To, class From> To* opaque(From* p) { return reinterpret_cast<To*>(p); }
VitaSystem* asSystem(FMOD::System* p) { return opaque<VitaSystem>(p); }
VitaSound* asSound(FMOD::Sound* p) { return opaque<VitaSound>(p); }
VitaControl* asControl(FMOD::ChannelControl* p) { return opaque<VitaControl>(p); }
VitaChannel* asChannel(FMOD::Channel* p) { return opaque<VitaChannel>(p); }
bool validVoice(const VitaChannel* c) {
    return c && c->system && c->system->initialized && c->system->engine.isValidVoiceHandle(c->handle);
}
void applyMaster(VitaGroup* g) {
    if (g && g->system->initialized) g->system->engine.setGlobalVolume(g->muted ? 0.f : g->volume);
}
FMOD_RESULT loadWave(VitaSystem* s, const char* data, FMOD_MODE mode,
                     FMOD_CREATESOUNDEXINFO* info, FMOD::Sound** out) {
    if (!s || !data || !out) return FMOD_ERR_INVALID_PARAM;
    *out = NULL;
    try {
    std::unique_ptr<VitaSound> sound(new VitaSound(s, mode));
    SoLoud::result error;
    // All supported containers use bounded streaming. Creating a sound retains
    // its normal FMOD handle/play semantics without eagerly decoding minutes
    // of PCM or constructing another full-sized WAV intermediate.
    if (mode & FMOD_OPENMEMORY) {
        if (!info || !info->length || info->length > 8u * 1024u * 1024u) return FMOD_ERR_INVALID_PARAM;
        error = sound->stream->loadMem(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), info->length, true, false);
        if (error != SoLoud::SO_NO_ERROR) {
            sound->stream.reset();
            error = sound->mp3Stream.load(NULL, reinterpret_cast<const unsigned char*>(data), info->length);
            sound->mp3 = error == SoLoud::SO_NO_ERROR;
        }
        sound->ownedBytes = info->length;
    } else {
        error = sound->stream->load(data);
        if (error != SoLoud::SO_NO_ERROR) {
            sound->stream.reset();
            error = sound->mp3Stream.load(data);
            sound->mp3 = error == SoLoud::SO_NO_ERROR;
        }
    }
    sound->streaming = !sound->mp3;
    if (error != SoLoud::SO_NO_ERROR)
        return error == SoLoud::OUT_OF_MEMORY ? FMOD_ERR_MEMORY : error == SoLoud::FILE_NOT_FOUND ? FMOD_ERR_FILE_NOTFOUND : FMOD_ERR_FORMAT;
    sound->source().setLooping((mode & FMOD_LOOP_NORMAL) != 0);
    sound->source().set3dProcessing((mode & FMOD_3D) != 0);
    const unsigned sampleFrames = sound->mp3 ? unsigned(std::ceil(sound->length()*sound->sampleRate())) : sound->stream->mSampleCount;
    sound->pcm = s->shortSounds.enqueue(data,mode,info ? info->length : 0,
        sampleFrames,sound->source().mChannels,sound->sampleRate(),sound->mp3);
    static std::atomic<unsigned> loads(0);
    if (loads.fetch_add(1) < 16)
        std::fprintf(stderr, "Vita audio loaded: rate=%u length_ms=%u 3d=%d\n",
            unsigned(sound->sampleRate()), unsigned(sound->length() * 1000.0),
            (mode & FMOD_3D) != 0);
    audioOwnedBytes.fetch_add(sound->ownedBytes + sizeof(VitaSound));
    size_t current = audioOwnedBytes.load();
    size_t peak = audioPeakBytes.load();
    while (peak < current && !audioPeakBytes.compare_exchange_weak(peak, current)) {}
    *out = opaque<FMOD::Sound>(sound.release());
    return FMOD_OK;
    } catch (const std::bad_alloc&) { return FMOD_ERR_MEMORY; }
      catch (...) { return FMOD_ERR_INTERNAL; }
}
}

extern "C" FMOD_RESULT F_API FMOD_System_Create(FMOD_SYSTEM **out) {
    if (!out) return FMOD_ERR_INVALID_PARAM;
    VitaSystem* s = new (std::nothrow) VitaSystem;
    if (!s) return FMOD_ERR_MEMORY;
    *out = opaque<FMOD_SYSTEM>(s); return FMOD_OK;
}
extern "C" FMOD_RESULT F_API FMOD_Memory_GetStats(int *current, int *maximum, FMOD_BOOL) {
    const size_t bytes = audioOwnedBytes.load() + VitaAudio::decoderBytes().load() + VitaAudio::pcmCacheBytes().load();
    size_t peak = audioPeakBytes.load();
    while (peak < bytes && !audioPeakBytes.compare_exchange_weak(peak, bytes)) {}
    if (current) *current = int(std::min<size_t>(bytes, 0x7fffffff));
    if (maximum) *maximum = int(std::min<size_t>(audioPeakBytes.load(), 0x7fffffff));
    return FMOD_OK;
}
extern "C" FMOD_RESULT F_API FMOD_Channel_GetUserData(FMOD_CHANNEL *channel, void **data) {
    if (!channel || !data) return FMOD_ERR_INVALID_PARAM;
    *data = opaque<VitaChannel>(channel)->userData; return FMOD_OK;
}

namespace FMOD {
FMOD_RESULT F_API System::release() {
    VitaSystem* s = asSystem(this);
    if(currentSystem==this){currentSystem=nullptr;++audioGeneration;}
    for(auto* c:s->channels)if(c->soundtrack && c->sound){
        auto* music=c->sound;s->engine.stop(c->handle);
        audioOwnedBytes.fetch_sub(music->ownedBytes+sizeof(VitaSound));
        for(auto* other:s->channels)if(other->sound==music)other->sound=nullptr;
        delete music;
    }
    s->shortSounds.stop();
    if (s->initialized) { s->engine.stopAll(); s->engine.deinit(); }
    for (size_t i = 0; i < s->channels.size(); ++i) delete s->channels[i];
    delete s->master; delete s; return FMOD_OK;
}
FMOD_RESULT F_API System::getNumDrivers(int *count) {
    if (!count) return FMOD_ERR_INVALID_PARAM; *count = 1; return FMOD_OK;
}
FMOD_RESULT F_API System::getDriverInfo(int id, char *name, int nameLength, FMOD_GUID *guid,
                                       int *rate, FMOD_SPEAKERMODE *mode, int *channels) {
    if (id != 0) return FMOD_ERR_INVALID_PARAM;
    if (name && nameLength > 0) { std::strncpy(name, "PS Vita Audio", nameLength - 1); name[nameLength - 1] = 0; }
    if (guid) std::memset(guid, 0, sizeof(*guid));
    if (rate) *rate = asSystem(this)->sampleRate;
    if (mode) *mode = FMOD_SPEAKERMODE_STEREO; if (channels) *channels = 2;
    return FMOD_OK;
}
FMOD_RESULT F_API System::setDriver(int driver) {
    if (driver != 0) return FMOD_ERR_INVALID_PARAM; asSystem(this)->driver = driver; return FMOD_OK;
}
FMOD_RESULT F_API System::getDriver(int *driver) {
    if (!driver) return FMOD_ERR_INVALID_PARAM; *driver = asSystem(this)->driver; return FMOD_OK;
}
FMOD_RESULT F_API System::setSoftwareFormat(int rate, FMOD_SPEAKERMODE, int) {
    // Preserve FMOD resampling through the checked 48 kHz native main port.
    (void)rate;
    asSystem(this)->sampleRate = 48000;
    return FMOD_OK;
}
FMOD_RESULT F_API System::getSoftwareFormat(int *rate, FMOD_SPEAKERMODE *mode, int *channels) {
    if (rate) *rate = asSystem(this)->sampleRate;
    if (mode) *mode = FMOD_SPEAKERMODE_STEREO; if (channels) *channels = 2; return FMOD_OK;
}
FMOD_RESULT F_API System::init(int maxChannels, FMOD_INITFLAGS, void *) {
    VitaSystem* s = asSystem(this);
    SoLoud::result error = s->engine.init(SoLoud::Soloud::CLIP_ROUNDOFF,
        SoLoud::Soloud::VITA_HOMEBREW, 48000, 2048, 2);
    if (error != SoLoud::SO_NO_ERROR) {
        std::fprintf(stderr, "Vita audio backend init failed: SoLoud %d (%s)\n",
            int(error), s->engine.getErrorString(error));
        return FMOD_ERR_OUTPUT_INIT;
    }
    s->sampleRate = 48000;
    s->initialized = true;
    currentSystem=this;++audioGeneration;
    s->shortSounds.start();
    std::fprintf(stderr, "Vita audio backend ready: %u Hz stereo, 2048 samples\n", s->sampleRate);
    if (maxChannels > 0) s->engine.setMaxActiveVoiceCount(std::min(maxChannels, 64));
    return FMOD_OK;
}
FMOD_RESULT F_API System::update() {
    VitaSystem* s = asSystem(this); if (!s->initialized) return FMOD_ERR_INITIALIZATION;
    const auto updateStart = std::chrono::steady_clock::now();
    // Storage logging belongs to housekeeping, never the deadline-sensitive
    // output thread. The mixer publishes counters without acquiring a file lock.
    if(RBX::vitaSlowJobLogging().load(std::memory_order_relaxed)&&updateStart-s->lastBudgetReport>=std::chrono::seconds(20)) {
        const auto maximum=RBX::vitaAudioMaxMixMicros().exchange(0,std::memory_order_relaxed);
        const auto overruns=RBX::vitaAudioMixOverruns().exchange(0,std::memory_order_relaxed);
        const auto gap=RBX::vitaAudioMaxBlockGapMicros().exchange(0,std::memory_order_relaxed);
        const auto late=RBX::vitaAudioLateBlocks().exchange(0,std::memory_order_relaxed);
        const auto drained=RBX::vitaAudioDrainedBlocks().exchange(0,std::memory_order_relaxed);
        std::fprintf(stderr,"Vita audio budget: max_mix_ms=%.2f overruns=%u samples=2048 max_block_gap_ms=%.2f late_blocks=%u drained_blocks=%u\n",double(maximum)/1000,overruns,double(gap)/1000,late,drained);
        s->lastBudgetReport=updateStart;
    }
    s->engine.update3dAudio();
    for (size_t i = 0; i < s->channels.size();) {
        VitaChannel* c = s->channels[i];
        if(c->soundtrack && validVoice(c)) {
            const unsigned loops=s->engine.getLoopCount(c->handle);
            if(loops!=c->musicLoopsReported) {
                std::fprintf(stderr,"Vita menu music loop completed: %u\n",loops);
                c->musicLoopsReported=loops;
            }
        }
        if (validVoice(c) && c->loopLimit >= 0 &&
            s->engine.getLoopCount(c->handle) - c->loopStart >= unsigned(c->loopLimit))
            s->engine.setLooping(c->handle, false);
        if (!validVoice(c)) {
            if (!c->reportedEnd) {
                c->reportedEnd = true;
                if (c->callback) c->callback(opaque<FMOD_CHANNELCONTROL>(c), FMOD_CHANNELCONTROL_CHANNEL,
                    FMOD_CHANNELCONTROL_CALLBACK_END, NULL, NULL);
            }
            // Roblox clears its FMOD channel pointer in the end callback. A
            // stopped channel has no owner after that point; retaining every
            // one until SoundService shuts down grows without bound in places
            // with frequent effects and makes each update progressively slower.
            // An interrupted setup may not have registered its end callback.
            // Keep that rare handle until shutdown instead of freeing an
            // object the game could still reference.
            if (c->callback && !c->userData) {
                delete c;
                s->channels[i] = s->channels.back();
                s->channels.pop_back();
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    const auto updateEnd = std::chrono::steady_clock::now();
    const double interval = s->lastUpdate.time_since_epoch().count() ?
        std::chrono::duration<double>(updateEnd - s->lastUpdate).count() : 0;
    s->updateCpu = interval > 0 ?
        100.0 * std::chrono::duration<double>(updateEnd - updateStart).count() / interval : 0;
    s->lastUpdate = updateEnd;
    return FMOD_OK;
}
FMOD_RESULT F_API System::set3DSettings(float doppler, float distance, float rolloff) {
    if (!std::isfinite(doppler) || !std::isfinite(distance) || !std::isfinite(rolloff) ||
        doppler < 0 || distance <= 0 || rolloff < 0) return FMOD_ERR_INVALID_PARAM;
    VitaSystem* s = asSystem(this); s->doppler = doppler; s->rolloff = rolloff;
    if (s->initialized) {
        s->engine.set3dSoundSpeed(343.3f / distance);
        for (auto* c : s->channels) if (validVoice(c)) {
            s->engine.set3dSourceDopplerFactor(c->handle, doppler);
            s->engine.set3dSourceAttenuation(c->handle,
                (c->mode & FMOD_3D_LINEARROLLOFF) ? SoLoud::AudioSource::LINEAR_DISTANCE : SoLoud::AudioSource::INVERSE_DISTANCE, rolloff);
        }
    }
    return FMOD_OK;
}
FMOD_RESULT F_API System::set3DListenerAttributes(int listener, const FMOD_VECTOR *pos,
    const FMOD_VECTOR *vel, const FMOD_VECTOR *forward, const FMOD_VECTOR *up) {
    if (listener != 0 || !pos || !forward || !up) return FMOD_ERR_INVALID_PARAM;
    const FMOD_VECTOR zero = {0, 0, 0}; if (!vel) vel = &zero;
    asSystem(this)->engine.set3dListenerParameters(pos->x, pos->y, pos->z,
        forward->x, forward->y, forward->z, up->x, up->y, up->z, vel->x, vel->y, vel->z);
    return FMOD_OK;
}
FMOD_RESULT F_API System::getVersion(unsigned int *version) {
    if (!version) return FMOD_ERR_INVALID_PARAM; *version = FMOD_VERSION; return FMOD_OK;
}
FMOD_RESULT F_API System::getChannelsPlaying(int *count) {
    if (!count) return FMOD_ERR_INVALID_PARAM;
    *count = asSystem(this)->engine.getActiveVoiceCount(); return FMOD_OK;
}
FMOD_RESULT F_API System::getCPUUsage(float *dsp, float *stream, float *geometry, float *updateValue, float *total) {
    if (dsp) *dsp=-1; if (stream) *stream=-1; if (geometry) *geometry=-1;
    if (updateValue) *updateValue=float(asSystem(this)->updateCpu); if (total) *total=-1;
    return FMOD_ERR_UNSUPPORTED; // Mixing runs in a backend thread; no measured CPU counter is exposed.
}
FMOD_RESULT F_API System::createSound(const char *data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *info, Sound **out) {
    return loadWave(asSystem(this), data, mode, info, out);
}
FMOD_RESULT F_API System::createStream(const char *data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *info, Sound **out) {
    return loadWave(asSystem(this), data, mode | FMOD_CREATESTREAM, info, out);
}
FMOD_RESULT F_API System::createChannelGroup(const char *, ChannelGroup **out) {
    if (!out) return FMOD_ERR_INVALID_PARAM;
    VitaSystem* s=asSystem(this); VitaGroup* g=new(std::nothrow) VitaGroup(s);
    if (!g) return FMOD_ERR_MEMORY; if (!s->master) s->master=g;
    *out=opaque<ChannelGroup>(g); return FMOD_OK;
}
FMOD_RESULT F_API System::getMasterChannelGroup(ChannelGroup **out) {
    if (!out) return FMOD_ERR_INVALID_PARAM; *out=opaque<ChannelGroup>(asSystem(this)->master);
    return *out ? FMOD_OK : FMOD_ERR_NOTREADY;
}
FMOD_RESULT F_API System::playSound(Sound *source, ChannelGroup *, bool paused, Channel **out) {
    if (!source || !out) return FMOD_ERR_INVALID_PARAM;
    *out = NULL;
    VitaSystem* s=asSystem(this); VitaSound* w=asSound(source);
    if (!s->initialized) return FMOD_ERR_INITIALIZATION;
    if (w->system != s) return FMOD_ERR_INVALID_PARAM;
    SoLoud::AudioSource& playback = w->playbackSource();
    SoLoud::handle h=(w->mode & FMOD_3D) ? s->engine.play3d(playback,0,0,0,0,0,0,1.f,paused)
                                           : s->engine.play(playback,1.f,0.f,paused);
    if (!h) return FMOD_ERR_CHANNEL_ALLOC;
    static std::atomic<unsigned> plays(0);
    if (plays.fetch_add(1) < 16)
        std::fprintf(stderr, "Vita audio playing: 3d=%d paused=%d\n",
            (w->mode & FMOD_3D) != 0, paused);
    VitaChannel* c=new(std::nothrow) VitaChannel(s,w,h);
    if (!c) { s->engine.stop(h); return FMOD_ERR_MEMORY; }
    try { s->channels.push_back(c); }
    catch (...) { s->engine.stop(h); delete c; *out=NULL; return FMOD_ERR_MEMORY; }
    s->engine.setVolume(h,userGameVolume.load());
    s->engine.set3dSourceDopplerFactor(h, s->doppler);
    s->engine.set3dSourceAttenuation(h, (c->mode & FMOD_3D_LINEARROLLOFF) ?
        SoLoud::AudioSource::LINEAR_DISTANCE : SoLoud::AudioSource::INVERSE_DISTANCE, s->rolloff);
    *out=opaque<Channel>(c); return FMOD_OK;
}
FMOD_RESULT F_API System::setReverbProperties(int, const FMOD_REVERB_PROPERTIES *) { return FMOD_ERR_UNSUPPORTED; }

FMOD_RESULT F_API Sound::release() {
    VitaSound* sound = asSound(this);
    for(auto* channel:sound->system->channels)if(channel->sound==sound)channel->sound=nullptr;
    audioOwnedBytes.fetch_sub(sound->ownedBytes + sizeof(VitaSound));
    delete sound; return FMOD_OK;
}
FMOD_RESULT F_API Sound::getDefaults(float *frequency, int *priority) {
    if (frequency) *frequency=asSound(this)->sampleRate(); if (priority) *priority=128; return FMOD_OK;
}
FMOD_RESULT F_API Sound::getLength(unsigned int *length, FMOD_TIMEUNIT unit) {
    if (!length) return FMOD_ERR_INVALID_PARAM;
    VitaSound* s=asSound(this); double seconds=s->length();
    if (unit==FMOD_TIMEUNIT_MS) *length=seconds*1000.0;
    else if (unit==FMOD_TIMEUNIT_PCM) *length=seconds*s->sampleRate();
    else return FMOD_ERR_UNSUPPORTED; return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::getSystemObject(System **out) {
    if (!out) return FMOD_ERR_INVALID_PARAM; *out=opaque<System>(asControl(this)->system); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::stop() {
    VitaControl* c=asControl(this);
    if (c->kind==ObjectChannel) c->system->engine.stop(static_cast<VitaChannel*>(c)->handle);
    else c->system->engine.stopAll(); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setPaused(bool paused) {
    VitaControl* c=asControl(this);
    if (c->kind==ObjectChannel) {
        auto* voice=static_cast<VitaChannel*>(c);c->system->engine.setPause(voice->handle,paused);
        static std::atomic<unsigned> reports(0);
        if(!paused && reports.fetch_add(1)<16)std::fprintf(stderr,
            "Vita audio unpaused: valid=%d volume=%.3f master=%.3f\n",
            validVoice(voice),c->volume,c->system->engine.getGlobalVolume());
    }
    else c->system->engine.setPauseAll(paused); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::getPaused(bool *paused) {
    if (!paused) return FMOD_ERR_INVALID_PARAM; VitaControl* c=asControl(this);
    *paused=c->kind==ObjectChannel ? c->system->engine.getPause(static_cast<VitaChannel*>(c)->handle) : false;
    return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setVolume(float volume) {
    VitaControl* c=asControl(this); c->volume=std::max(0.f,volume);
    if (c->kind==ObjectChannel) c->system->engine.setVolume(static_cast<VitaChannel*>(c)->handle,c->muted?0.f:c->volume*((static_cast<VitaChannel*>(c)->soundtrack||static_cast<VitaChannel*>(c)->userInterface)?1.f:userGameVolume.load()));
    else applyMaster(static_cast<VitaGroup*>(c)); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::getVolume(float *volume) {
    if (!volume) return FMOD_ERR_INVALID_PARAM; *volume=asControl(this)->volume; return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setMute(bool mute) {
    VitaControl* c=asControl(this); c->muted=mute;
    if (c->kind==ObjectChannel) c->system->engine.setVolume(static_cast<VitaChannel*>(c)->handle,mute?0.f:c->volume*((static_cast<VitaChannel*>(c)->soundtrack||static_cast<VitaChannel*>(c)->userInterface)?1.f:userGameVolume.load()));
    else applyMaster(static_cast<VitaGroup*>(c)); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::getMute(bool *mute) {
    if (!mute) return FMOD_ERR_INVALID_PARAM; *mute=asControl(this)->muted; return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setMode(FMOD_MODE mode) {
    VitaControl* base=asControl(this); if (base->kind!=ObjectChannel) return FMOD_OK;
    VitaChannel* c=static_cast<VitaChannel*>(base);
    const FMOD_MODE loopMask=FMOD_LOOP_OFF | FMOD_LOOP_NORMAL | FMOD_LOOP_BIDI;
    const FMOD_MODE rolloffMask=FMOD_3D_INVERSEROLLOFF | FMOD_3D_LINEARROLLOFF |
        FMOD_3D_LINEARSQUAREROLLOFF | FMOD_3D_INVERSETAPEREDROLLOFF | FMOD_3D_CUSTOMROLLOFF;
    // SoundChannel::update3D supplies a rolloff flag by itself. FMOD changes
    // that flag without disabling an independently selected loop mode.
    if (mode & loopMask)
        c->mode=(c->mode & ~loopMask) | (mode & loopMask);
    if (mode & (FMOD_3D_LINEARSQUAREROLLOFF | FMOD_3D_INVERSETAPEREDROLLOFF | FMOD_3D_CUSTOMROLLOFF | FMOD_LOOP_BIDI))
        return FMOD_ERR_UNSUPPORTED;
    if (mode & rolloffMask)
        c->mode=(c->mode & ~rolloffMask) | (mode & rolloffMask);
    if (mode & FMOD_3D)
        c->mode=(c->mode & ~FMOD_2D) | FMOD_3D;
    if (mode & FMOD_2D)
        c->mode=(c->mode & ~FMOD_3D) | FMOD_2D;
    c->system->engine.setLooping(c->handle,(c->mode & FMOD_LOOP_NORMAL)!=0 && c->loopLimit != 0);
    c->system->engine.set3dSourceAttenuation(c->handle, (c->mode & FMOD_3D_LINEARROLLOFF) ?
        SoLoud::AudioSource::LINEAR_DISTANCE : SoLoud::AudioSource::INVERSE_DISTANCE, c->system->rolloff);
    return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::getMode(FMOD_MODE *mode) {
    if (!mode) return FMOD_ERR_INVALID_PARAM; VitaControl* c=asControl(this);
    *mode=c->kind==ObjectChannel ? static_cast<VitaChannel*>(c)->mode : FMOD_DEFAULT; return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setCallback(FMOD_CHANNELCONTROL_CALLBACK callback) {
    VitaControl* c=asControl(this); if (c->kind!=ObjectChannel) return FMOD_ERR_BADCOMMAND;
    static_cast<VitaChannel*>(c)->callback=callback; return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::isPlaying(bool *playing) {
    if (!playing) return FMOD_ERR_INVALID_PARAM; VitaControl* c=asControl(this);
    *playing=c->kind==ObjectChannel && validVoice(static_cast<VitaChannel*>(c)); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::set3DAttributes(const FMOD_VECTOR *pos,const FMOD_VECTOR *vel,const FMOD_VECTOR *) {
    VitaControl* base=asControl(this); if (base->kind!=ObjectChannel || !pos) return FMOD_ERR_INVALID_PARAM;
    const FMOD_VECTOR zero={0,0,0}; if (!vel) vel=&zero; VitaChannel* c=static_cast<VitaChannel*>(base);
    c->system->engine.set3dSourceParameters(c->handle,pos->x,pos->y,pos->z,vel->x,vel->y,vel->z); return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::set3DMinMaxDistance(float minDistance,float maxDistance) {
    VitaControl* base=asControl(this); if (base->kind!=ObjectChannel) return FMOD_ERR_BADCOMMAND;
    VitaChannel* c=static_cast<VitaChannel*>(base); c->system->engine.set3dSourceMinMaxDistance(c->handle,minDistance,maxDistance);
    return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::setUserData(void *data) {
    VitaControl* c=asControl(this); if (c->kind!=ObjectChannel) return FMOD_ERR_BADCOMMAND;
    static_cast<VitaChannel*>(c)->userData=data; return FMOD_OK;
}
FMOD_RESULT F_API ChannelControl::getUserData(void **data) {
    if (!data) return FMOD_ERR_INVALID_PARAM; VitaControl* c=asControl(this);
    if (c->kind!=ObjectChannel) return FMOD_ERR_BADCOMMAND; *data=static_cast<VitaChannel*>(c)->userData; return FMOD_OK;
}

FMOD_RESULT F_API Channel::setFrequency(float frequency) {
    VitaChannel* c=asChannel(this);
    if (!validVoice(c) || !c->sound) return FMOD_ERR_INVALID_HANDLE;
    if (!std::isfinite(frequency) || frequency <= 0) return FMOD_ERR_INVALID_PARAM;
    c->frequency=frequency;
    float base=std::max(c->sound->sampleRate(),1.f);
    return c->system->engine.setRelativePlaySpeed(c->handle,frequency/base)==SoLoud::SO_NO_ERROR ? FMOD_OK:FMOD_ERR_INVALID_PARAM;
}
FMOD_RESULT F_API Channel::setPriority(int priority) { asChannel(this)->priority=priority; return FMOD_OK; }
FMOD_RESULT F_API Channel::setPosition(unsigned int position,FMOD_TIMEUNIT unit) {
    if (unit!=FMOD_TIMEUNIT_MS) return FMOD_ERR_UNSUPPORTED; VitaChannel* c=asChannel(this);
    if (!validVoice(c) || !c->sound) return FMOD_ERR_INVALID_HANDLE;
    if (c->sound->mp3 && position) return FMOD_ERR_UNSUPPORTED;
    if (position/1000.0 > c->sound->length()) return FMOD_ERR_INVALID_POSITION;
    // This pinned SoLoud version exposes void seek, so preflight what its
    // compatibility boundary can verify; unsupported MP3 seeks fail above.
    c->system->engine.seek(c->handle,position/1000.0); return FMOD_OK;
}
FMOD_RESULT F_API Channel::getPosition(unsigned int *position,FMOD_TIMEUNIT unit) {
    if (!position || unit!=FMOD_TIMEUNIT_MS) return FMOD_ERR_INVALID_PARAM; VitaChannel* c=asChannel(this);
    *position=c->system->engine.getStreamTime(c->handle)*1000.0; return FMOD_OK;
}
FMOD_RESULT F_API Channel::setChannelGroup(ChannelGroup* group) {
    return group && opaque<VitaGroup>(group) == asChannel(this)->system->master ? FMOD_OK : FMOD_ERR_UNSUPPORTED;
}
FMOD_RESULT F_API Channel::setLoopCount(int count) {
    if (count < -1) return FMOD_ERR_INVALID_PARAM;
    VitaChannel* c=asChannel(this); c->loopLimit=count;
    c->loopStart=c->system->engine.getLoopCount(c->handle);
    c->system->engine.setLooping(c->handle,count!=0); return FMOD_OK;
}
} // namespace FMOD

FMOD::System* vitaGameplayAudioSystem(){return currentSystem;}
unsigned vitaAudioGeneration(){return audioGeneration;}
void vitaSetMusicChannel(FMOD::Channel* channel){if(channel)asChannel(channel)->soundtrack=true;}
void vitaSetUiSoundChannel(FMOD::Channel* channel){if(channel)asChannel(channel)->userInterface=true;}
void vitaSetGameVolume(float volume){
    const float next=std::max(0.f,std::min(1.f,volume));if(userGameVolume.exchange(next)==next)return;
    if(currentSystem){auto* s=asSystem(currentSystem);for(auto* c:s->channels)if(validVoice(c)&&!c->soundtrack&&!c->userInterface)s->engine.setVolume(c->handle,c->muted?0.f:c->volume*next);}
}
