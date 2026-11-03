#pragma once
#include "Lua/LuaBridge.h"
#include "v8datamodel/TweenInfo.h"
namespace RBX { namespace Lua {
class TweenInfoBridge:public Bridge<TweenInfo> {
public:
    static void registerClassLibrary(lua_State* state);
    static int newTweenInfo(lua_State* state);
};
} }
