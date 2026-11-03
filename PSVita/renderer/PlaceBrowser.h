#pragma once
#include "LoaderNameSort.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "LoaderCatalog.h"
#include "LoaderPreferences.h"
#include "util/VitaPlaceFixes.h"

namespace VitaBrowser {
struct Place { std::string path, name, source; };

struct RuntimeSettings : UserPreferences
{
    bool autoAssetRepair;
    bool performanceLogging;
    bool prebuildSceneClusters;
    bool lock30Fps; // Legacy config field; fpsCap controls the current limiter.
    bool vsync;
    bool useSystemCore;
    unsigned placeFixes = RBX::VitaPlaceFixes::defaultMask;
    unsigned assetWorkers;
    unsigned graphicsQuality;
    std::string username;
    unsigned avatarPreset;
    bool avatarCustomColors;
    int avatarHeadColor;
    int avatarLeftArmColor;
    int avatarRightArmColor;
    int avatarTorsoColor;
    int avatarLeftLegColor;
    int avatarRightLegColor;

    RuntimeSettings(): autoAssetRepair(true), performanceLogging(false), prebuildSceneClusters(false), lock30Fps(true), vsync(true), useSystemCore(true),
        assetWorkers(2), graphicsQuality(1), username("VitaPlayer"), avatarPreset(0), avatarCustomColors(false),
        avatarHeadColor(24), avatarLeftArmColor(24), avatarRightArmColor(24), avatarTorsoColor(23),
        avatarLeftLegColor(37), avatarRightLegColor(37) {}
};

inline RuntimeSettings loadSettings()
{
    RuntimeSettings settings;
    bool haveButtonVolume=false;
    recoverAtomicTarget("ux0:/data/RBXV00001/system/settings.cfg");
    std::ifstream input("ux0:/data/RBXV00001/system/settings.cfg");
    std::string line;
    while (std::getline(input, line))
    {
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "auto_asset_repair")
            settings.autoAssetRepair = value != "0";
        else if (key == "place_fixes")
            settings.placeFixes = static_cast<unsigned>(std::strtoul(value.c_str(), NULL, 10)) & 127u;
        else if (key == "performance_logging")
            settings.performanceLogging = value == "1";
        else if (key == "prebuild_scene_clusters")
            settings.prebuildSceneClusters = value == "1";
        else if (key == "fps_lock_30")
            settings.lock30Fps = value != "0"; // New fps_cap takes precedence.
        else if (key == "vsync")
            settings.vsync = value != "0";
        else if (key == "cpu3")
            settings.useSystemCore = value != "0";
        else if (key == "asset_workers")
        {
            const int workers = std::atoi(value.c_str());
            settings.assetWorkers = static_cast<unsigned>(std::max(1, std::min(3, workers)));
        }
        else if (key == "graphics_quality")
        {
            const int quality = std::atoi(value.c_str());
            settings.graphicsQuality = static_cast<unsigned>(std::max(1, std::min(10, quality)));
        }
        else if (key == "username")
        {
            std::string clean;
            for (char c: value)
                if ((std::isalnum(static_cast<unsigned char>(c)) || c == '_') && clean.size() < 20)
                    clean += c;
            if (!clean.empty())
                settings.username = clean;
        }
        else if (key == "avatar_preset")
        {
            const int preset = std::atoi(value.c_str());
            settings.avatarPreset = static_cast<unsigned>(std::max(0, std::min(5, preset)));
        }
        else if (key == "avatar_custom_colors")
            settings.avatarCustomColors = value == "1";
        else if (key == "avatar_head_color")
            settings.avatarHeadColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if (key == "avatar_left_arm_color")
            settings.avatarLeftArmColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if (key == "avatar_right_arm_color")
            settings.avatarRightArmColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if (key == "avatar_torso_color")
            settings.avatarTorsoColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if (key == "avatar_left_leg_color")
            settings.avatarLeftLegColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if (key == "avatar_right_leg_color")
            settings.avatarRightLegColor = std::max(1, std::min(1032, std::atoi(value.c_str())));
        else if(key=="button_volume") haveButtonVolume=preferenceValue(settings,key,value)||haveButtonVolume;
        else preferenceValue(settings, key, value);
    }
    // Preserve the former half-game-volume feedback gain for old configs,
    // including an existing mute. New saves carry independent global controls.
    if(!haveButtonVolume)settings.buttonVolume=settings.gameVolume/2;
    return settings;
}

inline bool saveSettings(const RuntimeSettings& settings)
{
    sceIoMkdir("ux0:/data/RBXV00001", 0777);
    const char* tempPath = "ux0:/data/RBXV00001/system/settings.cfg.tmp";
    const char* finalPath = "ux0:/data/RBXV00001/system/settings.cfg";
    std::ofstream output(tempPath, std::ios::trunc);
    if (!output)
        return false;
    output << "auto_asset_repair=" << (settings.autoAssetRepair ? 1 : 0) << '\n'
        << "place_fixes=" << settings.placeFixes << '\n'
        << "performance_logging=" << (settings.performanceLogging ? 1 : 0) << '\n'
        << "prebuild_scene_clusters=" << (settings.prebuildSceneClusters ? 1 : 0) << '\n'
        << "fps_lock_30=" << (settings.fpsCap == 30) << '\n'
        << "vsync=" << (settings.vsync ? 1 : 0) << '\n'
        << "cpu3=" << (settings.useSystemCore ? 1 : 0) << '\n'
        << "asset_workers=" << settings.assetWorkers << '\n'
        << "graphics_quality=" << settings.graphicsQuality << '\n'
        << "username=" << settings.username << '\n'
        << "avatar_preset=" << settings.avatarPreset << '\n'
        << "avatar_custom_colors=" << (settings.avatarCustomColors ? 1 : 0) << '\n'
        << "avatar_head_color=" << settings.avatarHeadColor << '\n'
        << "avatar_left_arm_color=" << settings.avatarLeftArmColor << '\n'
        << "avatar_right_arm_color=" << settings.avatarRightArmColor << '\n'
        << "avatar_torso_color=" << settings.avatarTorsoColor << '\n'
        << "avatar_left_leg_color=" << settings.avatarLeftLegColor << '\n'
        << "avatar_right_leg_color=" << settings.avatarRightLegColor << '\n';
    writePreferences(output, settings);
    output.close();
    if (!output)
    {
        sceIoRemove(tempPath);
        return false;
    }
    if (!commitAtomicTemp(finalPath, tempPath)){sceIoRemove(tempPath);return false;}
    return true;
}


inline std::string placeSettingsPath(const std::string& place) {
    return "ux0:/data/RBXV00001/system/place-settings/"+placeSettingsKey(place)+".cfg";
}
inline bool placePreferenceKey(const std::string& key) {
    return key!="dark_mode" && key!="bloxicon_mode" && key!="show_chat_button" && key!="accept_circle" && key!="menu_music" && key!="music_volume" && key!="music_loop";
}
inline RuntimeSettings settingsForPlace(const RuntimeSettings& global,const std::string& place) {
    RuntimeSettings result=global;
    const std::string path=placeSettingsPath(place);recoverAtomicTarget(path);
    std::ifstream input(path);std::string line;
    if(!std::getline(input,line) || line!="place="+place)return result;
    while(std::getline(input,line)) {
        const auto equals=line.find('=');if(equals==std::string::npos)continue;
        const auto key=line.substr(0,equals),value=line.substr(equals+1);
        if(key=="graphics_quality") result.graphicsQuality=unsigned(std::max(1,std::min(10,std::atoi(value.c_str()))));
        else if(key=="vsync")result.vsync=value!="0";
        else if(key=="auto_asset_repair")result.autoAssetRepair=value!="0";
        else if(key=="prebuild_scene_clusters")result.prebuildSceneClusters=value=="1";
        else if(placePreferenceKey(key))preferenceValue(result,key,value);
    }
    return result;
}
inline bool savePlaceSettings(const RuntimeSettings& settings,const std::string& place) {
    if(place.empty() || place.find_first_of("\r\n")!=std::string::npos)return false;
    sceIoMkdir("ux0:/data/RBXV00001/system/place-settings",0777);
    const auto path=placeSettingsPath(place),temp=path+".tmp";
    std::ofstream output(temp,std::ios::trunc);
    output<<"place="<<place<<'\n'<<"graphics_quality="<<settings.graphicsQuality<<'\n'
        <<"vsync="<<settings.vsync<<'\n'<<"auto_asset_repair="<<settings.autoAssetRepair<<'\n'
        <<"prebuild_scene_clusters="<<settings.prebuildSceneClusters<<'\n';
    writePreferences(output,settings,true);output.close();
    if(!output || !commitAtomicTemp(path,temp)){sceIoRemove(temp.c_str());return false;}return true;
}

inline std::string takeLoaderMessage()
{
    const char* path = "ux0:/data/RBXV00001/system/loader-message.txt";
    recoverAtomicTarget(path);
    std::ifstream input(path);
    std::string message;
    std::getline(input, message);
    input.close();
    sceIoRemove(path);
    if (message.size() > 120)
        message.resize(120);
    return message;
}
inline bool supported(const std::string& name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".rbxl" || ext == ".rbxlx" || ext == ".rbxm" || ext == ".rbxmx";
}
inline void scan(const std::string& root, const std::string& source, std::vector<Place>& out, unsigned depth = 0)
{
    DIR* dir = opendir(root.c_str());
    if (!dir) return;
    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') continue;
        const std::string path = root + "/" + name;
        bool directory = false, regular = false;
#ifdef RBX_PLATFORM_PSVITA
        // Vita readdir (including packed directories) already returns SceIoStat.
        // Avoid reopening every entry just to ask for its file type again.
        const auto nativeMode = entry->d_stat.st_mode;
        if (SCE_S_ISDIR(nativeMode) || SCE_S_ISREG(nativeMode)) {
            directory = SCE_S_ISDIR(nativeMode);
            regular = SCE_S_ISREG(nativeMode);
        } else
#endif
        {
            struct stat info = {};
            if (stat(path.c_str(), &info) != 0) continue;
            directory = S_ISDIR(info.st_mode);
            regular = S_ISREG(info.st_mode);
        }
        if (directory && depth < 4) scan(path, source, out, depth + 1);
        else if (regular && supported(name)) out.push_back({path, name, source});
    }
    closedir(dir);
}
inline std::vector<Place> places()
{
    sceIoMkdir("ux0:/data/RBXV00001", 0777);
    sceIoMkdir("ux0:/data/RBXV00001/places", 0777);
    std::vector<Place> result;
    scan("ux0:/data/RBXV00001/places", "MY LEVELS", result);
    scan("app0:/maps", "INSTALLED", result);
    std::stable_sort(result.begin(), result.end(), [](const Place& a, const Place& b) {
        if (a.source != b.source) return a.source == "MY LEVELS";
        return foldedPlaceNameLess(a.name,b.name);
    });
    return result;
}

// A small built-in font keeps the launcher readable even when a place's asset
// pack or the engine font atlas is missing. Columns are five by seven pixels.
inline const unsigned char* glyph(char c)
{
    static const char names[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:/_-+()[]!?";
    static const unsigned char data[][5] = {
        {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
        {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
        {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
        {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
        {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
        {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
        {3,4,120,4,3},{97,81,73,69,67},
        {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
        {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
        {54,73,73,73,54},{6,73,73,41,30},
        {0,0,0,0,0},{0,96,96,0,0},{0,54,54,0,0},{32,16,8,4,2},
        {64,64,64,64,64},{8,8,8,8,8},{8,8,62,8,8},{0,28,34,65,0},
        {0,65,34,28,0},{0,127,65,65,0},{0,65,65,127,0},{0,0,95,0,0},{2,1,81,9,6}
    };
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const char* found = std::strchr(names, c);
    return data[found ? found - names : sizeof(data) / sizeof(data[0]) - 1];
}
inline void rect(float x, float y, float w, float h, unsigned color)
{
    glColor4ub((color >> 16) & 255, (color >> 8) & 255, color & 255, 255);
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}
inline void text(float x, float y, const std::string& value, unsigned color, int scale = 2, size_t maxChars = 72)
{
    static GLuint atlas = 0;
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:/_-+()[]!?";
    if (!atlas) {
        unsigned char pixels[64*64*4] = {};
        for (size_t i=0;i<chars.size();++i) {
            const unsigned char* g = glyph(chars[i]);
            for(int col=0;col<5;++col) for(int row=0;row<7;++row) {
                const size_t offset = (((i/8)*8+row)*64+(i%8)*8+col)*4;
                pixels[offset]=pixels[offset+1]=pixels[offset+2]=255;
                pixels[offset+3]=(g[col] & (1<<row)) ? 255 : 0;
            }
        }
        glGenTextures(1,&atlas); glBindTexture(GL_TEXTURE_2D,atlas);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,atlas);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub((color >> 16) & 255, (color >> 8) & 255, color & 255, 255);
    glBegin(GL_QUADS);
    for (size_t i = 0; i < value.size() && i < maxChars; ++i)
    {
        size_t index = chars.find(char(std::toupper(static_cast<unsigned char>(value[i]))));
        if(index==std::string::npos) index=chars.size()-1;
        float u=(index%8)*8.f/64, v=(index/8)*8.f/64;
        glTexCoord2f(u,v); glVertex2f(x,y);
        glTexCoord2f(u+5.f/64,v); glVertex2f(x+5*scale,y);
        glTexCoord2f(u+5.f/64,v+7.f/64); glVertex2f(x+5*scale,y+7*scale);
        glTexCoord2f(u,v+7.f/64); glVertex2f(x,y+7*scale);
        x += 6 * scale;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
}
inline void beginFrame()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glUseProgram(0);
    glViewport(0,0,960,544);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0); glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,960,544,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(.045f,.06f,.09f,1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

template<class Sampler> void showSettings(Sampler& sampler, RuntimeSettings& settings)
{
    unsigned selected = 0;
    unsigned previous = ~0u;
    uint64_t nextRepeat = 0;
    for (;;)
    {
        SceCtrlData pad = {}; SceTouchData rear = {}; std::deque<SceTouchData> touches;
        sampler.read(pad, touches, rear);
        unsigned pressed = pad.buttons & ~previous;
        const uint64_t now = sceKernelGetSystemTimeWide();
        const unsigned nav = pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
        if (nav && now >= nextRepeat)
        {
            pressed |= nav;
            nextRepeat = now + (nav == (previous & nav) ? 140000 : 350000);
        }
        if (!nav)
            nextRepeat = 0;
        previous = pad.buttons;

        if ((pressed & SCE_CTRL_UP) && selected) --selected;
        if ((pressed & SCE_CTRL_DOWN) && selected < 6) ++selected;
        const bool change = (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0;
        if (change)
        {
            if (selected == 0)
                settings.autoAssetRepair = !settings.autoAssetRepair;
            else if (selected == 1)
                settings.performanceLogging = !settings.performanceLogging;
            else if (selected == 2)
            {
                if (pressed & SCE_CTRL_LEFT)
                    settings.assetWorkers = settings.assetWorkers > 1 ? settings.assetWorkers - 1 : 3;
                else
                    settings.assetWorkers = settings.assetWorkers < 3 ? settings.assetWorkers + 1 : 1;
            }
            else if (selected == 3)
                settings.prebuildSceneClusters = !settings.prebuildSceneClusters;
            else if (selected == 4)
                settings.vsync = !settings.vsync;
            else if (selected == 5)
                settings.useSystemCore = !settings.useSystemCore;
            else
            {
                if (pressed & SCE_CTRL_LEFT)
                    settings.graphicsQuality = settings.graphicsQuality > 1 ? settings.graphicsQuality - 1 : 21;
                else
                    settings.graphicsQuality = settings.graphicsQuality < 21 ? settings.graphicsQuality + 1 : 1;
            }
        }
        if (pressed & SCE_CTRL_TRIANGLE)
            settings = RuntimeSettings();
        if (pressed & SCE_CTRL_CIRCLE)
        {
            saveSettings(settings);
            return;
        }

        beginFrame();
        rect(32,32,5,64,0x43d9ac); text(56,32,"BLOXICON / VITA SETTINGS",0xf4f7fb,4,32);
        text(58,77,"RUNTIME OPTIONS. NO PLACE-SPECIFIC OVERRIDES.",0xa1b3c7);

        const char* labels[] = {"AUTO ASSET REPAIR", "PERFORMANCE LOGS", "ASSET WORKERS", "PREBUILD SCENE", "VSYNC", "CAPUNLOCK CPU3", "GRAPHICS QUALITY"};
        for (unsigned i = 0; i < 7; ++i)
        {
            const float y = 88.f + i * 42.f;
            rect(32,y,896,40,i==selected?0x234e56:0x182232);
            if (i==selected) rect(32,y,4,40,0x43d9ac);
            text(52,y+8,labels[i],0xf4f7fb,2,42);
            std::string value;
            if (i == 0) value = settings.autoAssetRepair ? "ON" : "OFF";
            else if (i == 1) value = settings.performanceLogging ? "ON" : "OFF";
            else if (i == 2) value = std::to_string(settings.assetWorkers);
            else if (i == 3) value = settings.prebuildSceneClusters ? "ON" : "OFF";
            else if (i == 4) value = settings.vsync ? "ON" : "OFF";
	            else if (i == 5) value = settings.useSystemCore ? "PINNED" : "OFF";
	            else value = std::to_string(settings.graphicsQuality) + "/21";
            text(800,y+8,value,settings.performanceLogging && i==1?0xffb877:0x43d9ac,2,10);
            if (i == 0) text(52,y+31,"PUBLIC ASSET SOURCES + LOCAL CACHE. NO ACCOUNT TOKENS.",0xa1b3c7,1,92);
            if (i == 1) text(52,y+31,"DETAILED PROFILING. OFF IS FASTER FOR NORMAL PLAY.",0xa1b3c7,1,92);
            if (i == 2) text(52,y+31,"BACKGROUND CONTENT WORKERS. DEFAULT 2; RANGE 1-3.",0xa1b3c7,1,92);
            if (i == 3) text(52,y+29,"FINISH QUEUED CLUSTERS BEFORE PLAY. LONGER LOADS.",0xa1b3c7,1,92);
            if (i == 4) text(52,y+29,"SYNC PRESENTATION TO THE VITA DISPLAY. DEFAULT ON.",0xa1b3c7,1,92);
            if (i == 5) text(52,y+29,"TEXTURE DECODE + CONTENT REQUESTS USE CPU3 WHEN AVAILABLE.",0xa1b3c7,1,92);
            if (i == 6) text(52,y+29,"ORIGINAL BLOXICON RENDER QUALITY: DISTANCE, LIGHTING, LOD AND EFFECTS.",0xa1b3c7,1,92);
        }
        text(32,447,"CROSS/LEFT/RIGHT: CHANGE   TRIANGLE: DEFAULTS",0xa1b3c7,2,74);
        text(32,487,"CIRCLE: SAVE AND BACK",0x43d9ac,2,74);
        vglSwapBuffers(GL_FALSE);
        sceKernelDelayThread(8000);
    }
}

template<class Sampler> std::string choose(Sampler& sampler, RuntimeSettings& settings, std::string message = "")
{
    std::vector<Place> list = places();
    size_t selected = 0;
    if (message.empty())
        message = takeLoaderMessage();
    std::string last;
    std::ifstream recent("ux0:/data/RBXV00001/system/last-place.txt"); std::getline(recent, last);
    for (size_t i=0; i<list.size(); ++i) if (list[i].path == last) selected = i;
    unsigned previous = ~0u;
    bool wasTouch = false;
    uint64_t nextRepeat = 0;
    SceTouchPanelInfo panel = {}; sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel);
    for (;;)
    {
        SceCtrlData pad = {}; SceTouchData rear = {}; std::deque<SceTouchData> touches;
        sampler.read(pad, touches, rear);
        unsigned pressed = pad.buttons & ~previous;
        const uint64_t now = sceKernelGetSystemTimeWide();
        const unsigned nav = pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN);
        if (nav && now >= nextRepeat) { pressed |= nav; nextRepeat = now + (nav == (previous & nav) ? 140000 : 350000); }
        if (!nav) nextRepeat = 0;
        previous = pad.buttons;
        bool play = (pressed & SCE_CTRL_CROSS) != 0;
        if (pressed & SCE_CTRL_SQUARE)
        {
            showSettings(sampler, settings);
            saveSettings(settings);
            previous = ~0u;
        }
        if (pressed & SCE_CTRL_TRIANGLE) { list = places(); selected = std::min(selected, list.empty() ? size_t(0) : list.size()-1); message.clear(); }
        if (!list.empty())
        {
            if ((pressed & SCE_CTRL_UP) && selected) --selected;
            if ((pressed & SCE_CTRL_DOWN) && selected+1 < list.size()) ++selected;
            if (pressed & SCE_CTRL_LEFT) selected = selected >= 5 ? selected-5 : 0;
            if (pressed & SCE_CTRL_RIGHT) selected = std::min(selected+5,list.size()-1);
        }
        for (const SceTouchData& sample: touches)
        {
            if (sample.reportNum && !wasTouch)
            {
                float x = (sample.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                float y = (sample.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                if (y >= 138 && y < 438 && x >= 32 && x < 928)
                {
                    size_t row = selected/5*5 + size_t((y-138)/60);
                    if (row < list.size()) selected = row;
                }
                if (y >= 450 && y < 500) {
                    if (x >= 718) play = true;
                    else if (x < 180) { list = places(); selected = 0; message.clear(); }
                    else if (x < 400) selected = selected >= 5 ? selected-5 : 0;
                    else if (x < 620 && !list.empty()) selected = std::min(selected+5,list.size()-1);
                    else if (x >= 620 && x < 708) { showSettings(sampler, settings); saveSettings(settings); previous = ~0u; }
                }
            }
            wasTouch = sample.reportNum != 0;
        }
        if (play && !list.empty())
        {
            std::ifstream input(list[selected].path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() <= 0) message = "Cannot read this file. Choose another level.";
            else {
                std::ofstream save("ux0:/data/RBXV00001/system/last-place.txt"); save << list[selected].path << '\n';
                beginFrame(); text(40,220,"LOADING " + list[selected].name,0xffffff,3,47);
                text(40,278,"Large levels can take a minute to prepare.",0xa1b3c7);
                vglSwapBuffers(GL_FALSE);
                return list[selected].path;
            }
        }
        beginFrame();
        rect(32,32,5,64,0x43d9ac); text(56,32,"BLOXICON / VITA",0xf4f7fb,4,32);
        text(58,77,"YOUR LEVELS. ORIGINAL GAMEPLAY.",0xa1b3c7);
        text(32,113,message.empty() ? "ADD FILES: ux0:/data/RBXV00001/places" : message, message.empty()?0xa1b3c7:0xffb877,2,74);
        for (size_t i=selected/5*5; i<list.size() && i<selected/5*5+5; ++i) {
            const float y = 138 + (i%5)*60;
            rect(32,y,896,54,i==selected?0x234e56:0x182232);
            if(i==selected) rect(32,y,4,54,0x43d9ac);
            text(48,y+10,list[i].name,0xf4f7fb,2,59);
            text(48,y+33,list[i].source,0xa1b3c7,1,100);
        }
        if(list.empty()) text(50,210,"NO LEVELS FOUND. COPY YOUR PLACE FILES AND REFRESH.",0xf4f7fb);
        rect(32,450,145,45,0x182232); text(44,465,"REFRESH",0xa1b3c7);
        rect(190,450,180,45,0x182232); text(205,465,"PREV PAGE",0xa1b3c7);
        rect(384,450,180,45,0x182232); text(400,465,"NEXT PAGE",0xa1b3c7);
        rect(578,450,126,45,0x182232); text(591,465,"SETTINGS",0xa1b3c7,1,20);
        rect(718,450,210,45,0x43d9ac); text(742,465,"CROSS: PLAY",0x102a31);
        text(32,512,"D-PAD: SELECT   SQUARE: SETTINGS   TRIANGLE: REFRESH",0xa1b3c7,1,140);
        text(32,528,"IN GAME: START+SELECT RETURNS TO THIS LOADER",0xa1b3c7,1,140);
        vglSwapBuffers(GL_FALSE);
        sceKernelDelayThread(8000);
    }
}
}
