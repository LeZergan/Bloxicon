#include "lua.h"

// Older verified VM archives lack this optional optimization. Returning null
// selects LuaBridge's existing checked metatable path. A current VM provides
// the strong implementation and takes precedence over this compatibility hook.
__attribute__((weak)) void* lua_typeduserdatafast(lua_State*, int, const char*)
{
    return NULL;
}
