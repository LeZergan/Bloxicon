#pragma once
#include "AvatarProfile.h"
#include "LoaderAsyncTask.h"
#include <cstdint>

namespace VitaAvatar {
// Render-thread state only. The single save executor owns copies of storage,
// expected bytes and selection; it never owns a character or editor pointer.
template<class Storage>
class ProfileSave
{
    struct Result { bool success=false; std::string bytes,error; };
    VitaBrowser::LoaderAsyncTask<Result> work_;
    Profile desired_;
    std::string expected_,error_;
    uint64_t due_=0,started_=0;
    bool pending_=false,running_=false,failed_=false;
public:
    void reset(const std::string& expected) {
        work_.cancel(); expected_=expected;error_.clear();
        pending_=running_=failed_=false;
    }
    void request(const Profile& profile,uint64_t now) {
        if(failed_)return;
        desired_=profile;pending_=true;due_=now+300000;
    }
    bool busy() const {return pending_||running_;}
    bool failed() const {return failed_;}
    const std::string& error() const {return error_;}
    const std::string& expected() const {return expected_;}
    void cancel() {work_.cancel();pending_=running_=false;}
    // True only when the latest requested selection has verified durable bytes.
    bool poll(Storage storage,const typename VitaBrowser::LoaderAsyncTask<Result>::Schedule& schedule,uint64_t now)
    {
        if(failed_)return false;
        if(running_) {
            Result result;std::string error;
            const auto state=work_.poll(result,error);
            if(state==VitaBrowser::LoaderAsyncTask<Result>::Pending) {
                if(now-started_<=15000000)return false;
                work_.cancel();running_=pending_=false;failed_=true;
                error_="Avatar save timed out. Reopen to check your saved outfit.";return false;
            }
            running_=false;
            if(state!=VitaBrowser::LoaderAsyncTask<Result>::Ready || !result.success) {
                pending_=false;failed_=true;error_=result.error.empty()?"Avatar save was not confirmed. Reopen before editing again.":result.error;return false;
            }
            expected_=result.bytes;
            if(!pending_)return true;
            if(encodeProfile(desired_)==expected_){pending_=false;return true;}
        }
        if(!pending_ || now<due_)return false;
        const Profile profile=desired_;const std::string expected=expected_;
        pending_=false;started_=now;
        running_=work_.start(schedule,[storage,profile,expected](const std::atomic<bool>& cancelled) mutable {
            // Cancellation is allowed before commit. Once committed, always
            // finish readback, even if the screen was closed in the meantime.
            struct Guarded {
                Storage& storage;const std::atomic<bool>& cancelled;bool committed=false;
                bool read(std::string& value){return (committed||!cancelled.load())&&storage.read(value);}
                bool stage(const std::string& value){return !cancelled.load()&&storage.stage(value);}
                bool readStage(std::string& value){return !cancelled.load()&&storage.readStage(value);}
                bool commit(){if(cancelled.load())return false;committed=storage.commit();return committed;}
                void discardStage(){storage.discardStage();}
            } guarded{storage,cancelled};
            Result result;result.success=saveProfile(guarded,profile,expected,result.error);
            if(result.success)result.bytes=encodeProfile(profile);
            return result;
        });
        if(!running_){failed_=true;error_="Avatar save could not start. Reopen before editing again.";}
        return false;
    }
};
}
