#pragma once
#include "LoaderPreferences.h"
#include <fmod.hpp>
#include <string>
#include <fstream>
#include <vector>
#include <chrono>

// Gameplay publishes its existing audio system; menu music never opens a
// competing main audio port. The menu system is released before scene startup.
extern FMOD::System* vitaGameplayAudioSystem();
extern void vitaSetGameVolume(float volume);
extern void vitaSetMusicChannel(FMOD::Channel* channel);
extern unsigned vitaAudioGeneration();
namespace VitaBrowser {
class LoaderMusic {
    FMOD::System* menu=nullptr;
    FMOD::System* owner=nullptr;
    FMOD::Sound* sound=nullptr;
    FMOD::Channel* voice=nullptr;
    bool enabled=false,loop=true,inMenu=true,attempted=false;
    unsigned volume=35;
    unsigned generation=0;
    std::chrono::steady_clock::time_point nextUpdate{};
    void updateMenu(){
        const auto now=std::chrono::steady_clock::now();
        if(menu&&now>=nextUpdate){menu->update();nextUpdate=now+std::chrono::milliseconds(30);}
    }
    bool loadTrack(const char* path) {
        // Retain compressed bytes, not minutes of PCM. Mixer reads and loop
        // rewinds must not contend with wardrobe/thumbnail archive I/O.
        try {
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            if(!file)return false;
            const auto bytes=file.tellg();
            if(bytes<=0 || bytes>8*1024*1024)return false;
            std::vector<char> compressed(static_cast<size_t>(bytes));
            file.seekg(0);
            if(!file.read(compressed.data(),compressed.size()))return false;
            FMOD_CREATESOUNDEXINFO info={};info.cbsize=sizeof(info);info.length=unsigned(compressed.size());
            return owner->createStream(compressed.data(),FMOD_OPENMEMORY|FMOD_2D|
                (loop?FMOD_LOOP_NORMAL:FMOD_LOOP_OFF),&info,&sound)==FMOD_OK;
        } catch(const std::bad_alloc&) { return false; }
    }
    static FMOD_RESULT F_CALLBACK ended(FMOD_CHANNELCONTROL* control, FMOD_CHANNELCONTROL_TYPE,
        FMOD_CHANNELCONTROL_CALLBACK_TYPE type, void*, void*) {
        if(type==FMOD_CHANNELCONTROL_CALLBACK_END) {
            auto* channel=reinterpret_cast<FMOD::Channel*>(control);void* state=nullptr;
            channel->getUserData(&state);
            if(state && static_cast<LoaderMusic*>(state)->voice==channel)static_cast<LoaderMusic*>(state)->voice=nullptr;
            channel->setUserData(nullptr);
        }return FMOD_OK;
    }
    void stop() {
        // An already-destroyed gameplay system owns its sound/channel cleanup.
        if(owner && generation==vitaAudioGeneration() && (owner==menu || owner==vitaGameplayAudioSystem())) {
            if(voice)voice->stop();
            if(sound)sound->release();
        }
        voice=nullptr;sound=nullptr;owner=nullptr;attempted=false;nextUpdate={};
    }
    void startVoice() {
        if(sound && owner && owner->playSound(sound,nullptr,true,&voice)==FMOD_OK){
            vitaSetMusicChannel(voice);voice->setUserData(this);voice->setCallback(ended);
            voice->setMode(loop?FMOD_LOOP_NORMAL:FMOD_LOOP_OFF);
            voice->setVolume(volume/100.f);voice->setPaused(false);
        }
    }
public:
    // UI effects share the current menu output. Creating this system does not
    // enable the soundtrack or change any saved music/volume preference.
    FMOD::System* uiSystem(){
        if(!inMenu)return nullptr;
        if(!menu){if(FMOD::System_Create(&menu)!=FMOD_OK||menu->init(8,FMOD_INIT_NORMAL,nullptr)!=FMOD_OK){if(menu)menu->release();menu=nullptr;}}
        return menu;
    }
    void configure(const UserPreferences& settings,bool isMenu) {
        const bool volumeChanged=volume!=settings.musicVolume,loopChanged=loop!=settings.musicLoop;
        const bool next=settings.menuMusic && (isMenu || settings.musicInGame);
        if(inMenu!=isMenu){stop();if(menu){menu->release();menu=nullptr;}}
        if(enabled!=next)stop();
        inMenu=isMenu;enabled=next;volume=settings.musicVolume;loop=settings.musicLoop;
        if(volumeChanged||loopChanged)nextUpdate={};
        vitaSetGameVolume(settings.gameVolume/100.f);
        if(voice && owner && generation==vitaAudioGeneration() && (owner==menu || owner==vitaGameplayAudioSystem())) {
            if(volumeChanged)voice->setVolume(volume/100.f);
            if(loopChanged)voice->setMode(loop?FMOD_LOOP_NORMAL:FMOD_LOOP_OFF);
        }
    }
    void tick() {
        if(!enabled){updateMenu();return;} // Housekeep UI voices without starting music.
        if(owner && (generation!=vitaAudioGeneration() || (owner!=menu && owner!=vitaGameplayAudioSystem()))){owner=nullptr;sound=nullptr;voice=nullptr;attempted=false;}
        if(!owner) {
            if(vitaGameplayAudioSystem() && vitaGameplayAudioSystem()!=menu)owner=vitaGameplayAudioSystem();
            else if(inMenu) {
                if(!menu && !attempted) {
                    attempted=true;
                    if(FMOD::System_Create(&menu)!=FMOD_OK || menu->init(8,FMOD_INIT_NORMAL,nullptr)!=FMOD_OK){if(menu)menu->release();menu=nullptr;return;}
                    attempted=false;
                }
                owner=menu;
            }else owner=vitaGameplayAudioSystem();
        }
        if(owner && !attempted) {
            generation=vitaAudioGeneration();
            attempted=true;
            const char* paths[]={"ux0:/data/Bloxicon/assets/menu-music.mp3","ux0:/data/Bloxicon/content/music/menu.mp3","app0:/content/music/menu.mp3"};
            for(const auto path:paths)
                if(loadTrack(path))break;
            if(!sound)std::fprintf(stderr,"Vita menu music unavailable: no readable supported track\n");
            startVoice();
        }
        else if(owner && loop && sound && !voice)startVoice();
        // The audio worker continuously mixes independently of UI polling.
        // This housekeeping call need not acquire its mutex 250 times/sec.
        updateMenu();
    }
};
inline LoaderMusic& loaderMusic(){static LoaderMusic music;return music;}
inline void configureLoaderMusic(const UserPreferences& settings,bool menu){loaderMusic().configure(settings,menu);}
inline void loaderMusicTick(){loaderMusic().tick();}
}
