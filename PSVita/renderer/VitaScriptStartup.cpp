// Native local-host startup boundary. Reuse the already tested original
// ScriptContext implementation; do not replace its scheduler/VM to expose one
// existing private operation. This method adds no object state or Lua API.
#include "script/ScriptContext.h"
#include "network/Players.h"

#ifdef RBX_PLATFORM_PSVITA
namespace RBX
{
bool ScriptContext::startScriptsBeforeLocalJoin()
{
    RBXASSERT(Network::Players::backendProcessing(this));
    // Original pending-script queue: run ready scripts to first yield/return.
    // Never wait on arbitrary external modules or replay PlayerAdded events.
    startPendingScripts();
    const bool ready = pendingScripts.empty();
    if (ready)
        serverStartupPending = false;
    return ready;
}
}
#endif
