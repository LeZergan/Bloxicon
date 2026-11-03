#pragma once
#include <mutex>
#include <new>
#include <unordered_map>

namespace RBX {
// AnchorPoint must not enlarge GuiObject: retained engine helpers allocate
// derived widgets directly, in addition to the reflected instance factories.
class GuiAnchorPointStorage {
public:
    struct Point { float x, y; };
    Point get(const void* owner) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it=values.find(owner);
        return it==values.end()?Point{0,0}:it->second;
    }
    bool set(const void* owner,Point point) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it=values.find(owner);
        const Point before=it==values.end()?Point{0,0}:it->second;
        if(before.x==point.x&&before.y==point.y)return false;
        if(point.x==0&&point.y==0)values.erase(owner);
        else values[owner]=point;
        return true;
    }
    void erase(const void* owner) {
        std::lock_guard<std::mutex> lock(mutex);values.erase(owner);
    }
private:
    mutable std::mutex mutex;
    std::unordered_map<const void*,Point> values;
};
inline GuiAnchorPointStorage& guiAnchorPointStorage() {
    // Process lifetime also covers the engine's static GUI templates.
    // A widget can first reach this registry while unwinding an allocation
    // failure. Its default/erase paths must also work with an exhausted heap.
    alignas(GuiAnchorPointStorage) static unsigned char bytes[sizeof(GuiAnchorPointStorage)];
    static GuiAnchorPointStorage* storage=new (bytes) GuiAnchorPointStorage;
    return *storage;
}
}
