#pragma once
#include "network/OfflineAvatar.h"
#include "AvatarEditorBackend.h"
#include "AvatarEditQueue.h"
#include "AvatarCatalogSnapshot.h"
#include "AvatarCatalogMerge.h"
#include "AvatarPreviewCamera.h"
#include "AvatarRuntime.h"
#include "AvatarServerCatalogue.h"
#include "AvatarIdle.h"
#include "AvatarFrameStats.h"
#include "AvatarLoadGuard.h"
#include "AvatarProfileSave.h"
#include "AvatarOutfitActions.h"
#include "AvatarEditPlan.h"
#include "RuntimePack.h"
#include "LoaderAsyncTask.h"
#include "GfxCore/Device.h"
#include "GfxCore/Framebuffer.h"
#include "TextureGL.h"
#include "VisualEngine.h"
#include "SceneManager.h"
#include "SceneUpdater.h"
#include "SpatialHashedScene.h"
#include "CullableSceneNode.h"
#include "AdornRender.h"
#include "TextureManager.h"
#include "TextureCompositor.h"
#include "MaterialGenerator.h"
#include "Water.h"
#include "GfxBase/FrameRateManager.h"
#include "script/ScriptContext.h"
#include "util/ThreadPool.h"
#include "v8tree/Verb.h"
#include "util/SoundService.h"
#include "util/VitaPreviewSimulation.h"
#include "security/SecurityContext.h"
#include "v8datamodel/Camera.h"
#include "humanoid/Humanoid.h"
#include <functional>
#include <malloc.h>
#include <psp2/kernel/sysmem.h>

namespace VitaAvatar {
#ifdef RBX_PLATFORM_PSVITA
class ProfileStorage
{
    std::string path_;
public:
    explicit ProfileStorage(std::string path=profilePath()):path_(std::move(path)) {}
    bool read(std::string& value) { return readBounded(path_,kMaxProfileBytes,value,true); }
    bool stage(const std::string& value)
    {
        sceIoMkdir("ux0:/data/Bloxicon/system/avatar",0777);
        std::ofstream file(path_+".tmp",std::ios::binary|std::ios::trunc);
        file.write(value.data(),value.size()); file.flush();
        const bool written=file.good(); file.close();
        return written && !file.fail();
    }
    bool readStage(std::string& value) { return readBounded(path_+".tmp",kMaxProfileBytes,value); }
    bool commit() { return VitaBrowser::commitAtomicTemp(path_,path_+".tmp"); }
    void discardStage() { std::remove((path_+".tmp").c_str()); }
};
#endif
inline VerifiedOutfit verifyPreviewEdit(const Catalog& catalog,const Profile& effective,const Profile& delta,
    bool localOnly,bool checkWire,bool testing,const std::string& root,const std::atomic<bool>& cancelled,
    const OutfitBlobReader& remote = {})
{
    if(localOnly&&!checkWire){VerifiedOutfit result;result.profile=effective;result.success=true;return result;}
    // A server receives the whole outfit, including unchanged clothing and
    // accessories. Check its real encoder before any visible change or save.
    auto result=verifyOutfit(catalog,checkWire?effective:delta,testing,root,cancelled,remote);
    result.profile=effective;
    if(result.success&&checkWire)try {RBX::Network::makeOfflineAvatarWire(result);}
    catch(const std::exception& failure) {
        result.success=false;
        result.error=std::string("This outfit cannot be sent to a server. Previous items were kept. ")+failure.what();
    }
    return result;
}
inline bool loadServerWardrobe(std::shared_ptr<const Catalog>& snapshot,std::string& error)
{
    return serverCatalogue().read(snapshot,error);
}
inline bool scheduleAvatarWork(std::function<void()> work)
{
    static RBX::ThreadPool executor(1,RBX::BaseThreadPool::WaitForRunningTasks,2);
    return executor.schedule([work](boost::shared_ptr<rbx::spin_mutex>) { work(); });
}
inline bool scheduleAvatarSave(std::function<void()> work)
{
    static RBX::ThreadPool executor(1,RBX::BaseThreadPool::WaitForRunningTasks,2);
    return executor.schedule([work](boost::shared_ptr<rbx::spin_mutex>) { work(); });
}

class NativeEditor : public EditorBackend
{
    static constexpr unsigned previewWidth=304,previewHeight=304;
    RBX::Graphics::Device* device_;
    std::function<RBX::Graphics::VisualEngine&()> acquire_;
    std::function<bool()> diagnosticSetting_;
    RBX::Graphics::VisualEngine* visual_ = NULL;
    boost::shared_ptr<RBX::DataModel> dataModel_;
    boost::shared_ptr<RBX::ModelInstance> character_;
    RBX::Network::Player* player_ = NULL;
    boost::shared_ptr<RBX::Graphics::Texture> color_;
    boost::shared_ptr<RBX::Graphics::Renderbuffer> depth_;
    boost::shared_ptr<RBX::Graphics::Framebuffer> framebuffer_;
    std::shared_ptr<const Catalog> catalog_;
    Profile profile_;
    EditQueue edits_;
    ProfileStorage storage_;
    ProfileSave<ProfileStorage> saves_;
    OutfitHistory history_;
    std::array<Profile,6> outfits_;
    std::array<std::string,6> outfitBytes_;
    std::array<bool,6> outfitAvailable_{},outfitWritable_{},outfitReadable_{};
    struct OutfitSaveResult { unsigned slot=0;std::string bytes,error;bool success=false; };
    VitaBrowser::LoaderAsyncTask<OutfitSaveResult> outfitWork_;
    bool outfitSaving_=false;
    unsigned pendingOutfitSlot_=0;
    static std::string outfitPath(unsigned slot) {
        return "ux0:/data/Bloxicon/system/avatar/outfit-"+std::to_string(slot+1)+".v1";
    }
    std::string expected_, status_, exitMessage_;
    Feedback feedback_=Feedback::Ready;
    Appearance appearance_;
    PreviewCamera camera_;
    PreviewArea previewArea_=PreviewArea::Full;
    FrameStats frameStats_;
    VitaBrowser::LoaderAsyncTask<VerifiedOutfit> work_;
    std::vector<RBX::Graphics::CullableSceneNode*> boundsNodes_;
    bool testing_=false, readOnly_=false, bound_=false, frameBegun_=false;
    unsigned frame_=0, boundsRefresh_=0;
    bool appearanceReady_=false, diagnostics_=false,previewFailed_=false;
    bool previewAssetsPending_=true;
    LoadGuard loadGuard_;
    double lockMs_=0,prepareMs_=0,performMs_=0,drawMs_=0;
    unsigned timedFrames_=0;

    void reportResources(const char* phase)
    {
        if(!diagnostics_) return;
        SceKernelFreeMemorySizeInfo memory={}; memory.size=sizeof(memory);
        const int result=sceKernelGetFreeMemorySize(&memory);
        const auto heap=mallinfo();
        const auto frames=frameStats_.summary();
        RBX::Graphics::TextureManagerStats textures={};
        if(visual_) textures=visual_->getTextureManager()->getStatistics();
        std::fprintf(stderr,"Vita Avatar resources %s frames %u fps %.2f p50 %.2f p95 %.2f p99 %.2f worst %.2f memrc %d user %d cdram %d phycont %d heap-used %d heap-free %d tex-live %u:%u tex-orphan %u:%u queued %u\n",
            phase,frames.count,frames.fps,frames.p50,frames.p95,frames.p99,frames.worst,result,
            result>=0?memory.size_user:-1,result>=0?memory.size_cdram:-1,result>=0?memory.size_phycont:-1,
            heap.uordblks,heap.fordblks,textures.liveCount,textures.liveSize,
            textures.orphanedCount,textures.orphanedSize,textures.queuedCount);
        if(dataModel_){if(auto* run=RBX::ServiceProvider::find<RBX::RunService>(dataModel_.get())){
            const auto* physics=run->getPhysicsJob();const auto* heartbeat=run->getHeartbeat();
            const double n=std::max(1u,timedFrames_);
            std::fprintf(stderr,"Vita Avatar jobs %s physics_hz %.2f physics_duty %.2f physics_ms %.2f heartbeat_hz %.2f heartbeat_duty %.2f render_wall_ms %.2f/%.2f/%.2f/%.2f samples %u\n",phase,
                physics?physics->averageStepsPerSecond():0,physics?physics->averageDutyCycle():0,physics?physics->averageStepTime()*1000:0,
                heartbeat?heartbeat->averageStepsPerSecond():0,heartbeat?heartbeat->averageDutyCycle():0,
                lockMs_/n,prepareMs_/n,performMs_/n,drawMs_/n,timedFrames_);
        }}
        lockMs_=prepareMs_=performMs_=drawMs_=0;timedFrames_=0;
    }

    void updateBounds()
    {
        if(!character_ || !visual_) return;
        RBX::Extents extents=character_->computeExtentsWorld();
        if(!extents.isNull() && !extents.isNanInf())
        {
            boundsNodes_.clear();
            visual_->getSceneManager()->getSpatialHashedScene()->querySphere(boundsNodes_,extents.center(),128.f,0);
            for(auto* node:boundsNodes_)
            {
                const auto& bounds=node->getWorldBounds();
                if(!bounds.isNull() && !bounds.isNanInf()) extents.unionWith(bounds);
            }
            const auto& low=extents.min(); const auto& high=extents.max();
            camera_.bounds({{low.x,low.y,low.z}},{{high.x,high.y,high.z}});
        }
    }
    void pollAppearance()
    {
        if(!edits_.running()) { startNextEdit(); return; }
        if(loadGuard_.verificationExpired(sceKernelGetSystemTimeWide())) {
            work_.cancel();edits_.reject();loadGuard_.complete();
            exitMessage_=status_="This item took too long to load. Previous items were kept. Choose another item or try again.";
            feedback_=Feedback::Error;
            std::fprintf(stderr,"Vita Avatar verification timed out; previous outfit retained\n");
            return;
        }
        VerifiedOutfit reply;
        std::string message;
        if(!work_.settled())return;
        const auto state=work_.poll(reply,message);
        if(state==VitaBrowser::LoaderAsyncTask<VerifiedOutfit>::Pending) return;
        loadGuard_.complete();
        if(state==VitaBrowser::LoaderAsyncTask<VerifiedOutfit>::Cancelled) {
            edits_.retry();startNextEdit();return;
        }
        Profile completed;
        bool save=false;
        if(!edits_.complete(completed,save))
        {
            startNextEdit();
            return;
        }
        if(state!=VitaBrowser::LoaderAsyncTask<VerifiedOutfit>::Ready || !reply.success)
        {
            status_=reply.error.empty() ? "Could not read this outfit. Previous items were kept." : reply.error;
            edits_.reject(); exitMessage_=status_; feedback_=Feedback::Error;
            std::fprintf(stderr,"Vita Avatar asset request failed: %.256s\n",status_.c_str());
            return;
        }
        if(!appearance_.apply(reply,status_))
        { edits_.reject(); exitMessage_=status_; feedback_=Feedback::Error; return; }
        profile_=completed;appearanceReady_=true;
        const bool unavailable=!resolveProfile(profile_,*catalog_,testing_).unavailable.empty();
        edits_.accept(profile_);
        exitMessage_.clear();
        boundsRefresh_=4;
        if(save)
        {
            saves_.request(profile_,sceKernelGetSystemTimeWide());
            status_="Preview updated. Saving your outfit...";
            feedback_=Feedback::Updating;
        }
        else
        {
            feedback_=readOnly_ || unavailable ? Feedback::Warning : Feedback::Ready;
            status_=readOnly_ ? "Saved avatar needs recovery; changes are disabled." :
            (unavailable ? "Some saved items are unavailable. Manage them in Saved outfit." :
             (testing_ ? "Community wardrobe - visual results may vary" : "Your offline avatar"));
        }
        std::fprintf(stderr,"Vita Avatar applied components %u save_queued %d\n",unsigned(reply.models.size()),save?1:0);
    }
    bool startNextEdit()
    {
        Profile profile;
        if(!edits_.begin(profile)) return true;
        const auto resolved=resolveProfile(profile,*catalog_,testing_);
        auto index=catalog_;
        const std::string root=dataRoot();
        const bool test=testing_;
        const Profile effective=resolved.appearance;
        const auto plan=planEdit(profile_,effective,appearanceReady_,*index);
        if(!plan.valid){edits_.reject();exitMessage_=status_=plan.error;feedback_=Feedback::Error;return false;}
        const bool checkWire=appearanceReady_;
        const bool localOnly=plan.localOnly&&!checkWire;
        const Profile verify=plan.verify;
        const auto scheduler=localOnly ? std::function<bool(std::function<void()>)>([](std::function<void()> job){job();return true;}) :
            std::function<bool(std::function<void()>)>(scheduleAvatarWork);
        const auto remote=serverOutfitReader();
        const bool scheduled=work_.start(scheduler,[index,effective,verify,root,test,localOnly,checkWire,remote](const std::atomic<bool>& cancelled) {
            return verifyPreviewEdit(*index,effective,verify,localOnly,checkWire,test,root,cancelled,remote);
        });
        loadGuard_.begin(sceKernelGetSystemTimeWide());
        if(!scheduled)
        {
            edits_.reject();
            exitMessage_=status_="Wardrobe is busy. Your previous outfit was kept. Try again.";
            feedback_=Feedback::Error;
            return false;
        }
        status_=resolved.unavailable.empty() ? "Updating outfit..." : "Some saved items are unavailable. Other selections are kept.";
        feedback_=Feedback::Updating;
        return true;
    }
    bool queue(const Profile& profile, bool save)
    {
        const bool superseded=save && profile!=edits_.selection() && edits_.running();
        if(save) { if(!edits_.request(profile)) return false; }
        else edits_.reset(profile);
        if(superseded)work_.cancel();
        return startNextEdit();
    }
public:
    NativeEditor(RBX::Graphics::Device* device, std::function<RBX::Graphics::VisualEngine&()> acquire,std::function<bool()> diagnostics={})
        :device_(device),acquire_(std::move(acquire)),diagnosticSetting_(std::move(diagnostics)) { boundsNodes_.reserve(64); }
    ~NativeEditor() override { try { close(); } catch(...) {} }
    bool open(const Profile& legacy) override
    {
        if(dataModel_) return true;
        testing_=false;diagnostics_=diagnosticSetting_&&diagnosticSetting_();
        status_.clear(); exitMessage_.clear(); feedback_=Feedback::Ready; readOnly_=false; frame_=0; boundsRefresh_=4;
        previewFailed_=false;
        history_.clear();outfitSaving_=false;
        for(unsigned slot=0;slot<6;++slot) {
            VitaBrowser::recoverAtomicTarget(outfitPath(slot));
            ProfileStorage stored(outfitPath(slot));std::string error;
            outfitReadable_[slot]=outfitWritable_[slot]=stored.read(outfitBytes_[slot]);
            outfitAvailable_[slot]=outfitWritable_[slot]&&!outfitBytes_[slot].empty()&&
                parseProfile(outfitBytes_[slot],outfits_[slot],error)==ProfileRead::Valid;
            if(!outfitBytes_[slot].empty()&&!outfitAvailable_[slot])outfitWritable_[slot]=false;
        }
        previewArea_=PreviewArea::Full; camera_.clear();
        camera_.setAspectRatio(float(previewWidth)/previewHeight);
        previewAssetsPending_=true;
        loadGuard_=LoadGuard();
        frameStats_.reset();
        reportResources("before-open");
        const uint64_t openStart=sceKernelGetSystemTimeWide();
        auto phase=[&](const char* label){if(diagnostics_)std::fprintf(stderr,"Vita Avatar open phase %s elapsed_ms %.2f\n",label,double(sceKernelGetSystemTimeWide()-openStart)/1000.0);};
        try
        {
            if(!loadServerWardrobe(catalog_,status_))return false;
            phase("wardrobe");
            // Recover only the existing atomic-rename interruption; do not replace corrupt/future data.
            VitaBrowser::recoverAtomicTarget(profilePath());
            if(!storage_.read(expected_)) throw std::runtime_error("Could not read avatar settings");
            saves_.reset(expected_);
            profile_=legacy;
            if(!expected_.empty() && parseProfile(expected_,profile_,status_)!=ProfileRead::Valid)
            { profile_=legacy; readOnly_=true; }
            visual_=&acquire_();
            phase("renderer");
            {
                // Only this isolated preview is silent. SoundService is
                // attached during DataModel creation; suppress its port/job
                // here so the loader's existing music voice keeps playing.
                struct PreviewAudioScope {
                    bool previous=RBX::Soundscape::SoundService::soundDisabled;
                    PreviewAudioScope(){RBX::Soundscape::SoundService::soundDisabled=true;}
                    ~PreviewAudioScope(){RBX::Soundscape::SoundService::soundDisabled=previous;}
                } silence;
                // The loader owns the loading UI. Keep heartbeat for the
                // original Animate script without starting game LoadingScript.
                dataModel_=RBX::DataModel::createDataModel(true,new RBX::NullVerb(NULL,""),false);
                RBX::registerVitaPreviewModel(dataModel_.get(),dataModel_->getSyncronizationArbiter());
            }
            {
                RBX::DataModel::LegacyLock lock(dataModel_.get(),RBX::DataModelJob::Write);
                RBX::Security::Impersonator identity(RBX::Security::COM);
                auto scripts=RBX::ServiceProvider::create<RBX::ScriptContext>(dataModel_.get());
                RBX::ScriptContext::propScriptsDisabled.setValue(scripts,true);
                auto players=RBX::ServiceProvider::create<RBX::Network::Players>(dataModel_.get());
                players->setCharacterAutoSpawnProperty(false);
                dataModel_->processAfterLoad();
                auto created=players->createLocalPlayerForVita(-1,"Avatar");
                player_=RBX::Instance::fastDynamicCast<RBX::Network::Player>(created.get());
                if(!player_) throw std::runtime_error("Preview player could not be created");
                player_->loadCharacter(false,"");
                character_=RBX::Instance::fastSharedDynamicCast<RBX::ModelInstance>(shared_from(player_->getCharacter()));
                if(!character_) throw std::runtime_error("Original R6 character could not be created");
                appearance_.bind(character_);
                visual_->bindWorkspace(dataModel_); bound_=true;
                visual_->setViewport(previewWidth,previewHeight);
                auto camera=dataModel_->getWorkspace()->getCamera();
                camera->setCameraType(RBX::Camera::LOCKED_CAMERA);
                camera->setFieldOfViewDegrees(PreviewCamera::fieldOfView);
                camera->setViewport(G3D::Vector2int16(previewWidth,previewHeight));
                auto scene=visual_->getSceneManager();
                scene->setSkyEnabled(false);
                const auto bg=VitaBrowser::Design::surface;
                const G3D::Color3 previewBackground(((bg>>16)&255)/255.f,((bg>>8)&255)/255.f,(bg&255)/255.f);
                scene->setClearColor(G3D::Color4(previewBackground,1.f));
                // The standalone preview does not use RenderView's fog setup.
                // Zero FogParams would blend every visible part to black.
                scene->setFog(previewBackground,1000.f,2000.f);
                scene->setLighting(G3D::Color3(.52f,.52f,.52f),G3D::Vector3(-.4f,-.7f,-.6f).unit(),
                    G3D::Color3(.75f,.72f,.68f),G3D::Color3(.32f,.34f,.38f));
                updateBounds();
            }
            phase("rig");
            color_=device_->createTexture(RBX::Graphics::Texture::Type_2D,RBX::Graphics::Texture::Format_RGBA8,
                previewWidth,previewHeight,1,1,RBX::Graphics::Texture::Usage_Renderbuffer);
            depth_=device_->createRenderbuffer(RBX::Graphics::Texture::Format_D24S8,previewWidth,previewHeight,1);
            framebuffer_=device_->createFramebuffer(color_->getRenderbuffer(0,0),depth_);
            {
                RBX::DataModel::LegacyLock lock(dataModel_.get(),RBX::DataModelJob::Write);
                RBX::Security::Impersonator identity(RBX::Security::COM);
                startOriginalPreviewIdle(dataModel_.get(),character_.get());
                // The stock Animate script drives the original standing pose.
                // Keep it active while optimizing the isolated preview below.
                if (RBX::Humanoid* humanoid=RBX::Humanoid::modelIsCharacter(character_.get()))
                    humanoid->setPlatformStanding(true);
            }
            queue(profile_,false);
            phase("ready");
            reportResources("opened");
            std::fprintf(stderr,"Vita Avatar preview opened original R6, catalogue %u, test %d\n",unsigned(catalog_->items.size()),testing_?1:0);
            return true;
        }
        catch(const std::exception& error)
        {
            std::fprintf(stderr,"Vita Avatar open failed: %.256s\n",error.what());
            status_="Avatar preview could not be opened. Your saved avatar is unchanged.";
            feedback_=Feedback::Error;
            close();
            return false;
        }
    }
    void close() override
    {
        work_.cancel(); edits_.cancel(); saves_.cancel();
        if(frameBegun_) present();
        if(dataModel_)
        {
            RBX::DataModel::LegacyLock lock(dataModel_.get(),RBX::DataModelJob::Write);
            stopOriginalPreviewIdle(dataModel_.get());
        }
        reportResources("before-close");
        if(visual_ && bound_)
        {
            visual_->getTextureCompositor()->cancelPendingRequests();
            visual_->getTextureManager()->cancelPendingRequests();
            visual_->bindWorkspace(boost::shared_ptr<RBX::DataModel>());
            visual_->setViewport(960,544); bound_=false;
        }
        appearance_.clear(); appearanceReady_=false; character_.reset(); player_=NULL;
        if(dataModel_) { RBX::DataModel::closeDataModel(dataModel_); RBX::unregisterVitaPreviewModel(dataModel_.get()); dataModel_.reset(); }
        if(framebuffer_ || color_ || depth_) glFinish();
        framebuffer_.reset(); depth_.reset(); color_.reset();
        reportResources("closed");
        visual_=NULL;
        std::fprintf(stderr,"Vita Avatar preview closed; shared renderer retained\n");
    }
    const Catalog& catalog() const override { static const Catalog empty; return catalog_ ? *catalog_ : empty; }
    const Profile& selection() const override { return edits_.selection(); }
    bool request(const Profile& profile) override
    {
        if(!dataModel_ || !validProfile(profile)) return false;
        if(!VitaAccount::connector().editable()){status_=VitaAccount::connector().message();return false;}
        if(previewFailed_){status_="Preview interrupted. Reopen Avatar to retry. Your saved outfit is retained.";feedback_=Feedback::Error;return false;}
        if(readOnly_) { status_="Saved avatar needs recovery; no changes were written."; feedback_=Feedback::Error; return false; }
        if(profile==profile_ && !edits_.busy()) return true;
        const auto previous=selection();
        if(!queue(profile,true))return false;
        if(profile!=previous)history_.record(previous);
        return true;
    }
    bool applyAccountProfile(const std::string& bytes) override {
        if(busy()||readOnly_||previewFailed_)return false;
        Profile candidate;std::string error;
        if(parseProfile(bytes,candidate,error)!=ProfileRead::Valid)return false;
        if(candidate==selection())return true;
        return queue(candidate,true);
    }
    bool testing() const override { return testing_; }
    bool busy() const override { return edits_.busy() || saves_.busy() || outfitSaving_; }
    bool canUndo() const override { return dataModel_&&!previewFailed_&&!busy()&&!readOnly_&&history_.canUndo(); }
    bool canRedo() const override { return dataModel_&&!previewFailed_&&!busy()&&!readOnly_&&history_.canRedo(); }
    bool undo() override {
        if(!canUndo())return false;
        const auto old=selection(),target=history_.undoTarget();
        if(!queue(target,true))return false;
        history_.didUndo(old);return true;
    }
    bool redo() override {
        if(!canRedo())return false;
        const auto old=selection(),target=history_.redoTarget();
        if(!queue(target,true))return false;
        history_.didRedo(old);return true;
    }
    bool outfitAvailable(unsigned slot) const override { return slot<6&&outfitAvailable_[slot]; }
    bool outfitNeedsAttention(unsigned slot) const override { return slot<6&&!outfitWritable_[slot]; }
    bool wearOutfit(unsigned slot) override { return !busy()&&outfitAvailable(slot)&&request(outfits_[slot]); }
    bool storeOutfit(unsigned slot,bool remove) override {
        if(!dataModel_||slot>=6||busy()||readOnly_||!(remove?outfitReadable_[slot]:outfitWritable_[slot]))return false;
        const auto bytes=remove?std::string():encodeProfile(selection());
        const auto expected=outfitBytes_[slot];const auto path=outfitPath(slot);
        pendingOutfitSlot_=slot;
        outfitSaving_=outfitWork_.start(scheduleAvatarSave,[slot,bytes,expected,path](const std::atomic<bool>& cancelled) {
            OutfitSaveResult result;result.slot=slot;result.bytes=bytes;
            if(cancelled.load()){result.error="Outfit save cancelled.";return result;}
            ProfileStorage storage(path);
            result.success=saveOutfitSlot(storage,bytes,expected,result.error);
            return result;
        });
        status_=outfitSaving_?"Saving outfit slot...":"Outfit save could not start. Try again.";
        return outfitSaving_;
    }
    const std::string& status() const override { return status_; }
    std::string exitMessage() const override { return exitMessage_; }
    Feedback feedback() const override {
        if(feedback_==Feedback::Error)return feedback_;
        return busy() || previewAssetsPending_ ? Feedback::Updating : feedback_;
    }
    void setPreviewArea(PreviewArea area) override
    {
        if(area==previewArea_) return;
        previewArea_=area;
        switch(area)
        {
        case PreviewArea::Head: camera_.setView(.55f,.50f); break;
        case PreviewArea::UpperBody: camera_.setView(.18f,.68f); break;
        case PreviewArea::LowerBody: camera_.setView(-.24f,.72f); break;
        default: camera_.setView(0.f,1.f); break;
        }
    }
    void tickCatalogue() override { serverCatalogue().tick(); }
    bool updateCatalogue() override
    {
        serverCatalogue().tick();
        auto next=serverCatalogue().current();
        static const auto empty=std::make_shared<const Catalog>();
        if(!next)next=empty;
        if(next==catalog_)return false;
        catalog_=std::move(next);
        return true;
    }
    bool cataloguePending() const override { return serverCatalogue().pending(); }
    bool usesServerCatalogue() const override { return true; }
    std::string assetPath(const std::string& relative) const override
    {
        if(!safePath(relative))return {};
        if(usesServerCatalogue())return relative;
        const std::string local=std::string(dataRoot())+relative;
        return VitaBrowser::fileExists(local)?local:"app0:/content/avatar/"+relative;
    }
    ArtworkReader artworkReader(const std::string& path) const override
    {
        if(!usesServerCatalogue() || !catalog_)return {};
        const auto found=catalog_->blobs.find(path);
        if(found==catalog_->blobs.end())return {};
        CatalogueSession session;
        session.acquire();
        return [session,blob=found->second](std::string& bytes,const std::atomic<bool>& cancel) {
            return !cancel.load() && blob.bytes<=2*1024*1024 &&
                (verifyBlob(dataRoot(),blob,&bytes) || fetchCatalogueBlob(session,blob,bytes,cancel));
        };
    }
    unsigned render(float dt, float orbit, float zoom, bool resetCamera) override
    {
        if(!dataModel_ || !visual_ || !framebuffer_) return 0;
        if(frameBegun_) throw std::runtime_error("Avatar frame must be presented exactly once");
        if(diagnostics_ && frameStats_.sample(sceKernelGetSystemTimeWide()))
        { reportResources("browsing"); frameStats_.nextWindow(); }
        try
        {
            dt=std::max(.001f,std::min(.1f,dt));
            const uint64_t pollStart=diagnostics_?sceKernelGetSystemTimeWide():0;
            if(outfitSaving_) {
                OutfitSaveResult result;std::string error;
                const auto state=outfitWork_.poll(result,error);
                if(state!=VitaBrowser::LoaderAsyncTask<OutfitSaveResult>::Pending) {
                    outfitSaving_=false;
                    if(state==VitaBrowser::LoaderAsyncTask<OutfitSaveResult>::Ready&&result.success) {
                        outfitBytes_[result.slot]=result.bytes;
                        outfitWritable_[result.slot]=outfitReadable_[result.slot]=true;
                        outfitAvailable_[result.slot]=!result.bytes.empty();
                        if(!result.bytes.empty())parseProfile(result.bytes,outfits_[result.slot],error);
                        status_="Outfit slot saved on this Vita.";feedback_=Feedback::Saved;
                    } else {
                        outfitWritable_[pendingOutfitSlot_]=outfitReadable_[pendingOutfitSlot_]=false;
                        status_=result.error.empty()?"Outfit save could not be verified. Reopen Avatar.":result.error;
                        feedback_=Feedback::Error;
                    }
                }
            }
            // Persistence never acquires the DataModel lease or blocks drawing.
            if(saves_.poll(storage_,scheduleAvatarSave,sceKernelGetSystemTimeWide())) {
                expected_=saves_.expected();
                VitaAccount::connector().saved(expected_);
                std::fprintf(stderr,"Vita Avatar save verified %u bytes\n",unsigned(expected_.size()));
                if(!edits_.busy()) {
                    const bool unavailable=!resolveProfile(profile_,*catalog_,testing_).unavailable.empty();
                    status_=unavailable?"Outfit saved. Unavailable items remain in Saved outfit.":"Outfit saved. Ready for your next game.";
                    feedback_=unavailable?Feedback::Warning:Feedback::Saved;
                }
            }
            if(saves_.failed()) {
                readOnly_=true;exitMessage_=status_=saves_.error();feedback_=Feedback::Error;
                work_.cancel();edits_.reject();loadGuard_.complete();
            }
            if(previewFailed_)return 0;
            // Both avatar screens pace redraws and accumulate dt before
            // calling render. A second timer here can skip a 3D frame when
            // lock/acquisition costs move its deadline past the UI deadline.
            uint64_t prepareStart=0;
            G3D::Vector3 focus;
            {
                RBX::DataModel::LegacyLock lock(dataModel_.get(),RBX::DataModelJob::Write);
                RBX::Security::Impersonator identity(RBX::Security::COM);
                // Poll and prepare under the same write lease. Acquiring a
                // second legacy lease per frame needlessly marshals another
                // task to the scheduler and waits for its next wakeup.
                pollAppearance();
                if(diagnostics_)prepareStart=sceKernelGetSystemTimeWide();
                // Use the original rig's actual body-part location, not a
                // fraction of an outfit AABB that includes arbitrary hats.
                RBX::Humanoid* humanoid=RBX::Humanoid::modelIsCharacter(character_.get());
                RBX::PartInstance* subject=NULL;
                if(humanoid && previewArea_==PreviewArea::Head) subject=humanoid->getHeadSlow();
                else if(humanoid && previewArea_==PreviewArea::UpperBody) subject=humanoid->getVisibleTorsoSlow();
                if(subject)
                {
                    const auto point=subject->getCoordinateFrame().translation;
                    camera_.setSubject({{point.x,point.y,point.z}});
                }
                else if(humanoid && previewArea_==PreviewArea::LowerBody &&
                    humanoid->getLeftLegSlow() && humanoid->getRightLegSlow())
                {
                    const auto point=(humanoid->getLeftLegSlow()->getCoordinateFrame().translation+
                        humanoid->getRightLegSlow()->getCoordinateFrame().translation)*.5f;
                    camera_.setSubject({{point.x,point.y,point.z}});
                }
                else camera_.clearSubject();
                if(resetCamera) camera_.reset();
                camera_.step(dt,orbit,zoom);
                const auto position=camera_.position(), target=camera_.target();
                focus=G3D::Vector3(target[0],target[1],target[2]);
                auto camera=dataModel_->getWorkspace()->getCamera();
                camera->setCameraCoordinateFrame(G3D::CoordinateFrame(G3D::Vector3(position[0],position[1],position[2])));
                camera->lookAt(focus,false);
                camera->setCameraFocus(G3D::CoordinateFrame(focus));
                dataModel_->renderStep(dt);
                visual_->setCamera(*camera,focus);
                visual_->getFrameRateManager()->SubmitCurrentFrame(dt*1000,dt*1000,0,0);
                visual_->getAdorn()->preSubmitPass();
                visual_->getAdorn()->postSubmitPass();
                visual_->getSceneUpdater()->updatePrepare(frame_,*visual_->getUpdateFrustum());
                visual_->getWater()->update(dataModel_.get(),dt);
                visual_->getTextureCompositor()->update(focus);
            }
            visual_->getAdorn()->prepareRenderPass();
            const uint64_t performStart=diagnostics_?sceKernelGetSystemTimeWide():0;
            auto context=device_->beginFrame();
            if(!context) return 0;
            frameBegun_=true;
            context->setDefaultAnisotropy(std::max(1,visual_->getFrameRateManager()->getTextureAnisotropy()));
            visual_->getSceneUpdater()->updatePerform();
            visual_->getMaterialGenerator()->garbageCollectIncremental();
            visual_->getTextureManager()->garbageCollectIncremental();
            visual_->getTextureManager()->processPendingRequests();
            visual_->getTextureCompositor()->render(context);
            const uint64_t drawStart=diagnostics_?sceKernelGetSystemTimeWide():0;
            if(boundsRefresh_ || frame_%120==0)
            {
                RBX::DataModel::LegacyLock lock(dataModel_.get(),RBX::DataModelJob::Write);
                updateBounds();
                if(boundsRefresh_) --boundsRefresh_;
            }
            visual_->getSceneManager()->renderScene(context,framebuffer_.get(),visual_->getCamera(),previewWidth,previewHeight);
            if(diagnostics_){lockMs_+=(prepareStart-pollStart)*.001;prepareMs_+=(performStart-prepareStart)*.001;performMs_+=(drawStart-performStart)*.001;drawMs_+=(sceKernelGetSystemTimeWide()-drawStart)*.001;++timedFrames_;}
            // Clothing can still be waiting for its atlas bake after all source
            // PNGs have loaded. Do not announce a ready outfit until both queues
            // settle; retain the same bounded deadline and original animated rig.
            const auto pending=visual_->getTextureManager()->getStatistics().queuedCount+
                (visual_->getTextureCompositor()->isQueueEmpty()?0u:1u);
            const auto textureState=loadGuard_.textures(pending,sceKernelGetSystemTimeWide());
            previewAssetsPending_=busy() || textureState==LoadGuard::Loading;
            if(textureState==LoadGuard::Incomplete && feedback_!=Feedback::Error && feedback_!=Feedback::Warning) {
                status_="Some preview textures did not finish loading. You can keep editing or reopen Avatar to retry.";
                feedback_=Feedback::Warning;
                std::fprintf(stderr,"Vita Avatar preview texture deadline reached; animated rig remains visible\n");
            }
            ++frame_;
            return static_cast<RBX::Graphics::TextureGL*>(color_.get())->getId();
        }
        catch(const std::exception& error)
        {
            std::fprintf(stderr,"Vita Avatar render failed: %.256s\n",error.what());
            work_.cancel();edits_.reject();loadGuard_.complete();previewFailed_=true;
            exitMessage_=status_="Preview interrupted. Reopen Avatar to retry. Your saved outfit is retained.";
            feedback_=Feedback::Error;
            return 0;
        }
    }
    void present() override
    {
        if(frameBegun_) { device_->endFrame(); frameBegun_=false; }
        else vglSwapBuffers(GL_FALSE);
    }
};

class GameplayAppearance
{
    std::shared_ptr<const Catalog> catalog_;
    Profile saved_;
    Appearance appearance_;
    boost::weak_ptr<RBX::ModelInstance> observed_;
    VitaBrowser::LoaderAsyncTask<VerifiedOutfit> task_;
    bool enabled_=false, configured_=false, testing_=false, pending_=false;
    bool fallbackColors_=false, fallbackAfterAppearance_=false;
    bool refresh_=false;
    uint64_t retryAfter_=0;
public:
    GameplayAppearance()
    {
        std::string bytes,error;
        if(!readBounded(profilePath(),kMaxProfileBytes,bytes,true) || bytes.empty() ||
            parseProfile(bytes,saved_,error)!=ProfileRead::Valid) return;
        configured_=true;
        if(!saved_.enabled) return;

        if(!loadServerWardrobe(catalog_,error))
        {
            fallbackColors_=saved_.customColors;
            configured_=fallbackColors_;
            std::fprintf(stderr,"Vita Avatar gameplay catalogue unavailable; %s: %.160s\n",
                fallbackColors_?"using saved body colors":"using game appearance",error.c_str());
            return;
        }
        enabled_=true;
    }
    bool enabled() const { return enabled_; }
    bool configured() const { return configured_; }
    // The main/render thread receives this outside the DataModel lease. Only
    // immutable profile/catalogue data changes here; step commits appearance
    // through the same verified local/network path under the existing lease.
    bool applyAccountProfile(const std::string& bytes)
    {
        Profile candidate;std::string error;
        if(parseProfile(bytes,candidate,error)!=ProfileRead::Valid)return false;
        if(candidate==saved_ && (enabled_||!candidate.enabled))return true;
        if(candidate.enabled&&!catalog_){

            if(!loadServerWardrobe(catalog_,error))return false;
        }
        if(!VitaAccount::write(profilePath(),encodeProfile(candidate)))return false;
        saved_=std::move(candidate);configured_=enabled_=true;fallbackColors_=false;
        refresh_=true;retryAfter_=0;
        std::fprintf(stderr,"Vita Avatar queued live account outfit\n");
        return true;
    }
    void step(RBX::Network::Player* player)
    {
        if(!player) return;
        if(!enabled_)
        {
            if(!fallbackColors_ || !gameAllowsAvatar(player)) return;
            auto character=RBX::Instance::fastSharedDynamicCast<RBX::ModelInstance>(shared_from(player->getCharacter()));
            if(!character) return;
            const bool appearanceLoaded=player->getCharacterAppearanceLoaded();
            if(character==observed_.lock() && (!appearanceLoaded || fallbackAfterAppearance_)) return;
            observed_=character;
            fallbackAfterAppearance_=appearanceLoaded;
            boost::shared_ptr<RBX::BodyColors> colors;
            character->visitChildren([&](const boost::shared_ptr<RBX::Instance>& child) {
                if(!colors) colors=RBX::Instance::fastSharedDynamicCast<RBX::BodyColors>(child);
            });
            if(!colors) colors=RBX::Creatable<RBX::Instance>::create<RBX::BodyColors>();
            const auto& c=saved_.colors;
            RBX::BodyColors::prop_HeadColor.setValue(colors.get(),RBX::BrickColor(c[0]));
            RBX::BodyColors::prop_TorsoColor.setValue(colors.get(),RBX::BrickColor(c[1]));
            RBX::BodyColors::prop_LeftArmColor.setValue(colors.get(),RBX::BrickColor(c[2]));
            RBX::BodyColors::prop_RightArmColor.setValue(colors.get(),RBX::BrickColor(c[3]));
            RBX::BodyColors::prop_LeftLegColor.setValue(colors.get(),RBX::BrickColor(c[4]));
            RBX::BodyColors::prop_RightLegColor.setValue(colors.get(),RBX::BrickColor(c[5]));
            if(!colors->getParent()) colors->setParent(character.get());
            return;
        }
        auto character=RBX::Instance::fastSharedDynamicCast<RBX::ModelInstance>(shared_from(player->getCharacter()));
        auto previous=observed_.lock();
        const bool changedCharacter=character!=previous;
        if(changedCharacter || (refresh_&&sceKernelGetSystemTimeWide()>=retryAfter_))
        {
            task_.cancel(); pending_=false;
            if(changedCharacter){appearance_.clear();observed_=character;}
            if(!character || !gameAllowsAvatar(player))
            { refresh_=true;retryAfter_=sceKernelGetSystemTimeWide()+1000000;if(character) std::fprintf(stderr,"Vita Avatar respects place-owned character appearance\n");return; }
            if(changedCharacter)appearance_.bind(character);
            auto index=catalog_;
            const Profile effective=saved_.enabled?resolveProfile(saved_,*index,testing_).appearance:saved_;
            const bool test=testing_;
            const auto remote=serverOutfitReader();
            pending_=task_.start(scheduleAvatarWork,[index,effective,test,remote](const std::atomic<bool>& cancelled) {
                if(!effective.enabled){VerifiedOutfit result;result.profile=effective;result.success=true;return result;}
                return verifyOutfit(*index,effective,test,dataRoot(),cancelled,remote);
            });
            refresh_=!pending_;retryAfter_=sceKernelGetSystemTimeWide()+1000000;
        }
        if(!pending_ || !character) return;
        VerifiedOutfit result;
        std::string error;
        const auto state=task_.poll(result,error);
        if(state==VitaBrowser::LoaderAsyncTask<VerifiedOutfit>::Pending) return;
        pending_=false;
        if(state==VitaBrowser::LoaderAsyncTask<VerifiedOutfit>::Ready && result.success && gameAllowsAvatar(player))
        {
            try {
                const std::string wire=RBX::Network::makeOfflineAvatarWire(result);
                if(RBX::Network::submitOfflineAvatar(player,wire))
                    std::fprintf(stderr,"Vita Avatar gameplay submitted %u components (%u bytes)\n",unsigned(result.models.size()),unsigned(wire.size()));
                else
                {
                    std::fprintf(stderr,"Vita Avatar network submission was declined; applying the verified outfit locally\n");
                    if(!appearance_.apply(result,error))
                        std::fprintf(stderr,"Vita Avatar local fallback failed: %.200s\n",error.c_str());
                }
            } catch(const std::exception& exception) {
                std::fprintf(stderr,"Vita Avatar network submission failed: %.200s\n",exception.what());
                if(appearance_.apply(result,error)) std::fprintf(stderr,"Vita Avatar local appearance applied\n");
            }
        }
        else
        {
            std::fprintf(stderr,"Vita Avatar gameplay kept original items: %.200s\n",result.error.c_str());
            if(saved_.customColors)
            {
                enabled_=false;
                fallbackColors_=true;
                observed_.reset();
                fallbackAfterAppearance_=false;
                step(player);
            }
        }
    }
};
} // namespace VitaAvatar
