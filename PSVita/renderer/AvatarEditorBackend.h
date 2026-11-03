#pragma once
#include "AvatarProfile.h"
#include <atomic>
#include <functional>

namespace VitaAvatar {
// UI/platform boundary. The implementation renders the original Roblox character and scene.
class EditorBackend
{
public:
    enum class Feedback { Ready, Updating, Saved, Warning, Error };
    enum class PreviewArea { Full, Head, UpperBody, LowerBody };
    virtual ~EditorBackend() {}
    virtual bool open(const Profile& legacy) = 0;
    virtual void tickCatalogue() {}
    virtual bool usesServerCatalogue() const { return false; }
    virtual void close() = 0;
    virtual const Catalog& catalog() const = 0;
    virtual const Profile& selection() const = 0;
    virtual bool request(const Profile& selection) = 0;
    virtual bool applyAccountProfile(const std::string&) { return false; }
    virtual bool testing() const = 0;
    virtual bool busy() const = 0;
    virtual const std::string& status() const = 0;
    virtual std::string exitMessage() const { return std::string(); }
    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    virtual bool outfitAvailable(unsigned) const { return false; }
    virtual bool outfitNeedsAttention(unsigned) const { return false; }
    virtual bool wearOutfit(unsigned) { return false; }
    virtual bool storeOutfit(unsigned,bool) { return false; }
    virtual Feedback feedback() const
    { return busy() ? Feedback::Updating : (exitMessage().empty() ? Feedback::Ready : Feedback::Error); }
    virtual void setPreviewArea(PreviewArea) {}
    virtual unsigned render(float dt, float orbit, float zoom, bool resetCamera) = 0;
    virtual void present() = 0;
    virtual std::string assetPath(const std::string& relative) const = 0;
    using ArtworkReader = std::function<bool(std::string&,const std::atomic<bool>&)>;
    virtual ArtworkReader artworkReader(const std::string&) const { return {}; }
};
} // namespace VitaAvatar
