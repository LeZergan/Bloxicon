#pragma once

#ifdef RBX_PLATFORM_PSVITA
namespace RBX {
// A VM operation retains ownership through native callbacks and exceptions.
// No Lua state/global-state fields are added: retained engine ABI is unchanged.
void lockVitaLua();
void unlockVitaLua();
class VitaLuaScope {
public:
    VitaLuaScope() { lockVitaLua(); }
    ~VitaLuaScope() { unlockVitaLua(); }
    VitaLuaScope(const VitaLuaScope&) = delete;
    VitaLuaScope& operator=(const VitaLuaScope&) = delete;
};
}
#define RBX_VITA_LUA_SCOPE RBX::VitaLuaScope vitaLuaScope
#else
#define RBX_VITA_LUA_SCOPE ((void)0)
#endif
