#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace VitaBrowser {
// Diagnostic page state must never make input wait for storage. A single
// worker drains the newest requested page, coalescing rapid tab round trips.
class LoaderStateWriter {
    struct State {
        std::mutex mutex;
        std::string latest;
        bool pending=false, scheduled=false;
    };
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    using Schedule=std::function<bool(std::function<void()>)>;
    using Write=std::function<void(const std::string&)>;
    bool request(const std::string& page,const Schedule& schedule,const Write& write) {
        const auto shared=state;
        {
            std::lock_guard<std::mutex> guard(shared->mutex);
            shared->latest=page;shared->pending=true;
            if(shared->scheduled)return true;
            shared->scheduled=true;
        }
        bool accepted=false;
        try {
            accepted=schedule([shared,write] {
                for(;;) {
                    std::string value;
                    {
                        std::lock_guard<std::mutex> guard(shared->mutex);
                        if(!shared->pending){shared->scheduled=false;return;}
                        value=shared->latest;shared->pending=false;
                    }
                    // No state lock is held during a potentially slow write.
                    try {write(value);} catch(...) {}
                }
            });
        } catch(...) {}
        if(!accepted) {
            std::lock_guard<std::mutex> guard(shared->mutex);
            shared->scheduled=false;
        }
        return accepted;
    }
};
}
