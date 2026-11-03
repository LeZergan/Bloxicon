#pragma once

#include <exception>
#include <new>

namespace VitaLoadLifecycle {

class CleanupFailed : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "Failed to release an incomplete place; restarting the loader safely.";
    }
};

// Body-local locks/objects unwind before the existing engine shutdown runs.
// Successful loads keep their ownership; no callback/type-erasure allocation is
// needed while recovering from allocation failure.
template<class Body, class Close>
auto transaction(Body body, Close close) -> decltype(body())
{
    try
    {
        return body();
    }
    catch (...)
    {
        try { close(); }
        catch (...) { throw CleanupFailed(); }
        throw;
    }
}

} // namespace VitaLoadLifecycle
