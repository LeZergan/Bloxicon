#pragma once

namespace VitaBrowser {

// A modal owns every press accumulated while it was open. Resume the parent
// only after a neutral sample, discarding that release frame as well. The
// caller feeds the existing sampler's last touch state, including empty queues.
class LoaderInputBoundary
{
    bool waiting=false;
public:
    void waitForRelease() { waiting=true; }
    bool blocked() const { return waiting; }
    bool suppress(unsigned buttons, bool touching)
    {
        if (!waiting) return false;
        if (!buttons && !touching) waiting=false;
        return true;
    }
};
}
