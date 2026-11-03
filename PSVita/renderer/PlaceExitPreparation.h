#pragma once
#include <atomic>
#include <functional>
#include <thread>

namespace VitaLoading {
// World/GPU destruction belongs to process replacement. Only scripts, durable
// writes and network disconnects run here, while the UI thread keeps drawing.
class PlaceExitPreparation {
public:
    enum Phase { Callbacks, Saves, Network, Ready, Failed };
private:
    std::atomic<Phase> phase_{Callbacks};
    std::atomic<bool> callbackError_{false};
    std::thread worker_;
public:
    PlaceExitPreparation(std::function<void()> callbacks, std::function<void()> saves,
                         std::function<void()> network)
        : worker_([this,callbacks,saves,network] {
            try { callbacks(); } catch (...) { callbackError_.store(true); }
            // A script error must never skip already accepted saves.
            phase_.store(Saves,std::memory_order_release);
            try { saves(); }
            catch (...) { phase_.store(Failed,std::memory_order_release);return; }
            phase_.store(Network,std::memory_order_release);
            try { network(); }
            catch (...) { phase_.store(Failed,std::memory_order_release);return; }
            phase_.store(Ready,std::memory_order_release);
        }) {}
    ~PlaceExitPreparation() { if(worker_.joinable())worker_.join(); }
    Phase phase() const { return phase_.load(std::memory_order_acquire); }
    bool callbackError() const { return callbackError_.load(); }
    bool done() const { const auto p=phase();return p==Ready||p==Failed; }
    PlaceExitPreparation(const PlaceExitPreparation&)=delete;
    PlaceExitPreparation& operator=(const PlaceExitPreparation&)=delete;
};
}
