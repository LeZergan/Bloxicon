// Included inside VitaBrowser after the shared loader primitives.
inline VitaAvatar::Profile avatarLegacyProfile(const RuntimeSettings& settings)
{
    VitaAvatar::Profile profile;
    profile.enabled=avatarOverrideEnabled(settings);
    profile.customColors=profile.enabled;
    const AvatarColors colors=avatarColors(settings);
    profile.colors={{unsigned(avatarBrickColorFromRgb(colors.head)),unsigned(avatarBrickColorFromRgb(colors.torso)),
        unsigned(avatarBrickColorFromRgb(colors.leftArm)),unsigned(avatarBrickColorFromRgb(colors.rightArm)),
        unsigned(avatarBrickColorFromRgb(colors.leftLeg)),unsigned(avatarBrickColorFromRgb(colors.rightLeg))}};
    return profile;
}
inline void avatarDrawRenderTexture(unsigned texture, const VitaAvatar::Editor::Rect& box)
{
    if(!texture) return;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texture);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    // DeviceGL and the checked HAVE_UNFLIPPED_FBOS driver render offscreen
    // textures in image row order. An extra presentation flip inverts the rig.
    const float top=0.f,bottom=1.f;
    uiTextureRect({box.x,box.y,box.w,box.h},Design::imageRadius,top,bottom);
    glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
}
inline float avatarStick(unsigned char value,unsigned deadzonePercent=18)
{
    // SceCtrl's centered byte has 128 negative steps and 127 positive steps.
    // Using 127 for both sides turns a resting -23 into a tiny active signal
    // at the default 18% dead zone, repeatedly boosting idle redraws.
    const int delta=int(value)-128;
    const float axis=delta/(delta<0?128.f:127.f);
    const float deadzone=std::min(deadzonePercent,60u)*.01f;
    const float amount=std::max(0.f,(std::fabs(axis)-deadzone)/(1.f-deadzone));
    return axis<0 ? -amount : amount;
}
inline void avatarSetPreset(VitaAvatar::Profile& profile,unsigned preset)
{
    if(!preset) { profile.enabled=false; return; }
    const AvatarColors colors=avatarColors(preset);
    profile.enabled=true; profile.customColors=true;
    profile.colors={{unsigned(avatarBrickColorFromRgb(colors.head)),unsigned(avatarBrickColorFromRgb(colors.torso)),
        unsigned(avatarBrickColorFromRgb(colors.leftArm)),unsigned(avatarBrickColorFromRgb(colors.rightArm)),
        unsigned(avatarBrickColorFromRgb(colors.leftLeg)),unsigned(avatarBrickColorFromRgb(colors.rightLeg))}};
}

inline std::string avatarArtworkPath(const VitaAvatar::Item& item,const VitaAvatar::EditorBackend& backend)
{
    if(item.category==VitaAvatar::Category::Head && !backend.usesServerCatalogue()){
        const auto derived="app0:/ui2016/head-previews/"+artworkKey(item.id)+".png";
        if(fileExists(derived))return derived;
    }
    // Retrieved catalogue artwork already depicts the original item. Prefer it
    // to cropping the clothing template, which cannot show the complete item.
    if(!item.thumbnail.empty())
        return backend.assetPath(item.thumbnail);
    if(item.category==VitaAvatar::Category::Shirt || item.category==VitaAvatar::Category::Pants)
        for(const auto& path:item.dependencies)
            if(path.compare(0,9,"textures/")==0)
                return backend.assetPath(path);
    return {};
}

inline bool avatarDrawItemArtwork(LoaderFileTexture& image,const std::string& path,
    VitaAvatar::Category category,const VitaAvatar::Editor::Rect& box,
    const VitaAvatar::EditorBackend::ArtworkReader& read = {})
{
    if(path.empty() || !image.loadAsync(path,read)) return false;
    if(path.find("head-previews/")!=std::string::npos || path.find("thumbnails3d/")!=std::string::npos || path.find("thumbnails/")!=std::string::npos)
        return image.drawFit(path,box.x+10,box.y+8,box.w-20,box.h-48,Design::imageRadius);
    if(category==VitaAvatar::Category::Shirt)
    {
        // Map the source shirt's front UV faces onto an R6 torso and arms.
        // This shows the actual material in wear positions, not its flat sheet.
        const float x=box.x+60,y=box.y+10;
        uiRect(x+26,y+11,46,66,Design::muted);
        uiRect(x+4,y+16,20,57,Design::muted);
        uiRect(x+74,y+16,20,57,Design::muted);
        image.drawRegion(path,x+26,y+11,46,66,.39f,.13f,.61f,.39f);
        image.drawRegion(path,x+4,y+16,20,57,.07f,.13f,.29f,.39f);
        image.drawRegion(path,x+74,y+16,20,57,.71f,.13f,.93f,.39f);
        return true;
    }
    if(category==VitaAvatar::Category::Pants)
    {
        const float x=box.x+67,y=box.y+10;
        uiRect(x+8,y+6,68,30,Design::muted);
        uiRect(x+9,y+37,29,51,Design::muted);
        uiRect(x+46,y+37,29,51,Design::muted);
        image.drawRegion(path,x+8,y+6,68,30,.39f,.13f,.61f,.36f);
        image.drawRegion(path,x+9,y+37,29,51,.18f,.64f,.40f,.94f);
        image.drawRegion(path,x+46,y+37,29,51,.60f,.64f,.82f,.94f);
        return true;
    }
    return image.drawFit(path,box.x+37,box.y+8,140,box.h-48,Design::imageRadius);
}

inline const char* avatarPrimaryAction(const VitaAvatar::Editor::Tab& tab,
    const VitaAvatar::Profile& outfit,bool categoryFocus)
{
    using VitaAvatar::Editor::Kind;
    if(categoryFocus) return "Open category";
    if(tab.kind==Kind::Colors) return "Choose color";
    if(tab.kind==Kind::Presets) return tab.selected ? "Apply colors" : "Use game look";
    if(tab.kind==Kind::Saved) return "Remove item";
    if(tab.selected==0) return "Reset category";
    const auto* item=tab.focusedItem();
    if(item&&!item->browseable(true))return "Preview only";
    return item && VitaAvatar::accessoryCategory(item->category) &&
        VitaAvatar::Editor::isEquipped(outfit,*item) ? "Remove & save" : "Wear & save";
}

inline const char* avatarCategoryHelp(const VitaAvatar::Editor::Tab& tab,bool categoryFocus)
{
    using VitaAvatar::Editor::Kind;
    if(categoryFocus) return "Up / down: category. Confirm: open items.";
    if(tab.kind==Kind::Colors) return "Choose a body part. Confirm: open the color palette.";
    if(tab.kind==Kind::Presets) return "Presets change colors. Your items stay saved.";
    if(tab.kind==Kind::Saved) return "Remove saved items, including unavailable ones.";
    if(const auto* item=tab.focusedItem())if(!item->browseable(true))return "Original preview. This item is not available to wear.";
    if(tab.category==VitaAvatar::Category::Shirt || tab.category==VitaAvatar::Category::TShirt)
        return "3D upper-body preview. Confirm equips and saves.";
    if(tab.category==VitaAvatar::Category::Pants)
        return "3D leg preview. Confirm equips and saves.";
    if(tab.category==VitaAvatar::Category::Package)
        return "3D body preview. Confirm equips and saves.";
    return "Confirm: wear and save. The left view shows your saved outfit.";
}

inline const char* avatarFeedbackTitle(VitaAvatar::EditorBackend::Feedback state,bool enabled)
{
    using Feedback=VitaAvatar::EditorBackend::Feedback;
    switch(state)
    {
    case Feedback::Updating: return "Updating outfit...";
    case Feedback::Saved: return "Saved on this Vita";
    case Feedback::Warning: return "Outfit needs attention";
    case Feedback::Error: return "Outfit update failed";
    default: return enabled ? "Your outfit is active" : "Using the game's appearance";
    }
}

inline VitaAvatar::EditorBackend::PreviewArea avatarPreviewArea(const VitaAvatar::Editor::Tab& tab)
{
    using Area=VitaAvatar::EditorBackend::PreviewArea;
    if(tab.kind!=VitaAvatar::Editor::Kind::Items) return Area::Full;
    switch(tab.category)
    {
    case VitaAvatar::Category::Face:
    case VitaAvatar::Category::Head:
    case VitaAvatar::Category::Hat:
    case VitaAvatar::Category::Hair:
        return Area::Head;
    case VitaAvatar::Category::Shirt:
    case VitaAvatar::Category::TShirt:
        return Area::UpperBody;
    case VitaAvatar::Category::Accessory:
        // Generic accessories include back/waist/shoulder items; keep the full
        // body visible instead of assuming they all live near the torso.
        return Area::Full;
    case VitaAvatar::Category::Pants:
        return Area::LowerBody;
    default:
        return Area::Full;
    }
}

template<class Sampler>
void showAvatarImportHelp(Sampler& sampler,const RuntimeSettings& settings,LoaderInputHints& hints,const std::string& message=std::string())
{
    SceCtrlData pad={};SceTouchData rear={};std::deque<SceTouchData> touches;
    sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned previous=pad.buttons;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
    SceTouchData initial={};sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);bool wasTouch=initial.reportNum!=0;
#ifdef __vita__
    LoaderRenderCadence redraw(100000);
#endif
    for(;;) {
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned pressed=pad.buttons&~previous;previous=pad.buttons;
        for(const auto& sample:touches) {
            if(sample.reportNum&&!wasTouch) {
                const float x=(sample.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                const float y=(sample.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                pressed|=loaderControlMask(hints.hit(x,y));
            }
            wasTouch=sample.reportNum!=0;
        }
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE))return;
#ifdef __vita__
        if(!loaderMenuRedraw(redraw,pad,touches))continue;
#endif
        loaderFrame();drawChrome(PageAvatar,settings,message.empty()?"Import your own avatar items":"Outfit details");
        const auto box=Design::dialogPanel();uiRoundRect(box.x,box.y,box.w,box.h,10,kUiPanel);
        if(!message.empty()) {
            uiText(48,108,"YOUR OUTFIT",kUiMuted,16,864,true);
            uiText(48,136,"Outfit details",kUiText,28,864,true);
            const auto lines=Geometry::paragraphLines(message,864,8,[](const std::string& text){return loaderFont().measure(text,22);});
            for(size_t i=0;i<lines.size();++i)uiText(48,190+float(i)*28,lines[i],kUiText,22,864);
        } else {
        uiText(48,108,"YOUR WARDROBE",kUiMuted,16,864,true);
        uiText(48,136,"Personal wardrobe imports",kUiText,28,864,true);
        uiText(48,184,"Use the included avatar_import_local tool on your computer.",kUiText,20,864);
        uiText(48,220,"Choose a face, shirt, pants or T-shirt image, or a classic XML model.",kUiMuted,18,864);
        uiText(48,264,"Copy its content/avatar folder into:",kUiMuted,18,864);
        uiText(48,292,"ux0:/data/RBXV00001/content/avatar",Design::focus,20,864,true);
        uiText(48,356,"Reopen Avatar. Your imported items appear beside the community wardrobe.",kUiMuted,18,864);
        }
        hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Done"},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}});
        vglSwapBuffers(GL_FALSE);
    }
}

inline Design::Rect accessoryFitField(size_t index) {
    return {340.f+float(index%2)*308.f,112.f+float(index/2)*112.f,300,104};
}
template<class Sampler>
void showAccessoryFit(Sampler& sampler,const RuntimeSettings& settings,VitaAvatar::EditorBackend& backend,
    LoaderInputHints& hints,const std::string& id)
{
    SceCtrlData pad={};SceTouchData rear={},touch={};std::deque<SceTouchData> touches;
    sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned previous=pad.buttons;size_t field=1;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1);bool wasTouch=touch.reportNum!=0;
    uint64_t then=sceKernelGetSystemTimeWide(),repeat=0;
#ifdef __vita__
    LoaderRenderCadence redraw;
    float renderDt=0;
#endif
    static const char* labels[]={"Left / right","Down / up","Back / forward","Pitch","Yaw","Roll"};
    for(;;) {
        VitaAccount::connector().tick([&](const std::string& bytes){return backend.applyAccountProfile(bytes);},!backend.busy());
        if(std::find(backend.selection().accessories.begin(),backend.selection().accessories.end(),id)==backend.selection().accessories.end())return;
        const auto now=sceKernelGetSystemTimeWide();const float dt=std::min(.1f,float(now-then)*.000001f);then=now;
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned pressed=pad.buttons&~previous;
        const unsigned directions=pad.buttons&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        if(directions&&now>=repeat){pressed|=directions;repeat=now+120000;}previous=pad.buttons;
        for(const auto& sample:touches) {
            if(sample.reportNum&&!wasTouch) {
                const float x=(sample.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                const float y=(sample.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                for(size_t i=0;i<6;++i)if(accessoryFitField(i).contains(x,y)) {
                    field=i;const auto b=accessoryFitField(i);
                    if(x<b.x+54)pressed|=SCE_CTRL_LEFT;else if(x>=b.x+b.w-54)pressed|=SCE_CTRL_RIGHT;
                }
                pressed|=loaderControlMask(hints.hit(x,y));
            }
            wasTouch=sample.reportNum!=0;
        }
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE))return;
        if(pressed&SCE_CTRL_UP)field=field>=2?field-2:field;
        if(pressed&SCE_CTRL_DOWN)field=field+2<6?field+2:field;
        auto profile=backend.selection();auto values=profile.accessoryFit.count(id)?profile.accessoryFit.at(id):std::array<int,6>{};
        bool changed=false;
        if(pressed&SCE_CTRL_TRIANGLE){profile.accessoryFit.erase(id);changed=true;}
        else if(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
            const int delta=(pressed&SCE_CTRL_RIGHT)?5:-5;
            const int limit=field<3?300:180;values[field]=std::max(-limit,std::min(limit,values[field]+delta));
            if(std::all_of(values.begin(),values.end(),[](int value){return value==0;}))profile.accessoryFit.erase(id);
            else profile.accessoryFit[id]=values;
            profile.enabled=true;changed=true;
        }
        changed=changed && profile!=backend.selection();
        if(changed)backend.request(profile);
        const float orbit=avatarStick(pad.rx,settings.stickDeadzone),zoom=avatarStick(pad.ry,settings.stickDeadzone);
#ifdef __vita__
        renderDt=std::min(.1f,renderDt+dt);
        if(!redraw.due(now,pad.buttons||wasTouch||changed||std::fabs(orbit)>.001f||std::fabs(zoom)>.001f,unsigned(field),backend.busy())){
            loaderMusicTick();sceKernelDelayThread(redraw.pollDelay(now));continue;
        }
#endif
        backend.setPreviewArea(VitaAvatar::EditorBackend::PreviewArea::Full);
#ifdef __vita__
        const auto texture=backend.render(renderDt,orbit,zoom,false);renderDt=0;
#else
        const auto texture=backend.render(dt,orbit,zoom,false);
#endif
        loaderFrame();drawChrome(PageAvatar,settings,"Accessory fit / Changes save automatically");
        const auto& preview=VitaAvatar::Editor::preview;
        uiRoundRect(preview.x,preview.y,preview.w,preview.h,10,kUiPanel);
        avatarDrawRenderTexture(texture,{preview.x+6,preview.y+6,preview.w-12,preview.w-12});
        uiText(340,60,"Accessory fit",kUiText,Design::title,608,true);
        uiText(340,Design::headingCaptionY,"Adjust fit / Right stick: rotate and zoom",kUiMuted,16,608);
        const auto& current=backend.selection();const auto fit=current.accessoryFit.find(id);
        for(size_t i=0;i<6;++i){const auto b=accessoryFitField(i);const int value=fit==current.accessoryFit.end()?0:fit->second[i];
            uiRoundRect(b.x,b.y,b.w,b.h,10,i==field?kUiPanelSelected:kUiPanel);
            if(i==field)uiFocusFrame(b,Design::focus);
            uiText(b.x+64,b.y+29,labels[i],kUiMuted,18,b.w-128,true);
            std::ostringstream number;if(i<3){number.setf(std::ios::fixed);number.precision(2);number<<value*.01f<<" studs";}else number<<value<<" deg";
            uiText(b.x+64,b.y+55,number.str(),kUiText,20,b.w-128,true);
            uiText(b.x+16,b.y+38,"-",Design::focus,28,30,true);uiText(b.x+b.w-38,b.y+38,"+",Design::focus,28,30,true);
        }
        hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Done"},{RBX::SDLK_GAMEPAD_BUTTONY,"Reset fit"},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}},13);
        backend.present();
    }
}

template<class Sampler>
void showAvatarOutfits(Sampler& sampler,const RuntimeSettings& settings,VitaAvatar::EditorBackend& backend,LoaderInputHints& hints)
{
    backend.setPreviewArea(VitaAvatar::EditorBackend::PreviewArea::Full);
    size_t selected=0;int armed=-1;unsigned previous=~0u;
    uint64_t then=sceKernelGetSystemTimeWide(),repeat=0;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
    SceTouchData initial={};sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);
    bool wasTouch=initial.reportNum!=0;std::string message;
    Design::StickNavigation stick;
#ifdef __vita__
    LoaderRenderCadence redraw;float renderDt=0;
#endif
    for(;;) {
        VitaAccount::connector().tick([&](const std::string& bytes){return backend.applyAccountProfile(bytes);},!backend.busy());
        SceCtrlData pad={};SceTouchData rear={};std::deque<SceTouchData> touches;
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);
        applyLoaderStick(pad,stick);
        const auto now=sceKernelGetSystemTimeWide();
        unsigned pressed=pad.buttons&~previous;
        const unsigned directions=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        if(directions&&(directions!=(previous&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT))||now>=repeat)) {
            pressed|=directions;repeat=now+160000;
        }
        previous=pad.buttons;
        for(const auto& touch:touches) {
            if(touch.reportNum&&!wasTouch) {
                const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                const float y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                const int hit=VitaAvatar::Editor::outfitHit(x,y);
                if(hit>=0){selected=size_t(hit);pressed|=SCE_CTRL_CROSS;}
                pressed|=loaderControlMask(hints.hit(x,y));
            }
            wasTouch=touch.reportNum!=0;
        }
        if(pressed&SCE_CTRL_CIRCLE)return;
        if(pressed&SCE_CTRL_START) {
            showAvatarImportHelp(sampler,settings,hints);
            sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);wasTouch=initial.reportNum!=0;
            previous=~0u;then=sceKernelGetSystemTimeWide();continue;
        }
        const auto oldSelected=selected;
        if(pressed&SCE_CTRL_UP)selected=VitaAvatar::Editor::gridMove(selected,8,0,-1);
        if(pressed&SCE_CTRL_DOWN)selected=VitaAvatar::Editor::gridMove(selected,8,0,1);
        if(pressed&SCE_CTRL_LEFT)selected=VitaAvatar::Editor::gridMove(selected,8,-1,0);
        if(pressed&SCE_CTRL_RIGHT)selected=VitaAvatar::Editor::gridMove(selected,8,1,0);
        if(oldSelected!=selected){armed=-1;message.clear();loaderUiSounds().outcome(false,true,false,now);}
        if((pressed&SCE_CTRL_CROSS)&&!backend.busy()) {
            const bool applied=selected==0?backend.undo():selected==1?backend.redo():backend.wearOutfit(unsigned(selected-2));
            message=applied?"Updating your outfit...":"No outfit change is available here.";armed=-1;
            if(applied)loaderUiSounds().outcome(false,false,true,now);
        }
        const bool save=(pressed&SCE_CTRL_SQUARE)!=0,remove=(pressed&SCE_CTRL_TRIANGLE)!=0;
        if(selected>=2&&(save||remove)&&!backend.busy()) {
            const unsigned slot=unsigned(selected-2);const int action=int(selected)*2+int(remove);
            if((remove||backend.outfitAvailable(slot))&&armed!=action) {
                armed=action;message=remove?"Press Delete again to clear this slot.":"Press Save again to replace this outfit.";
            } else {
                const bool accepted=backend.storeOutfit(slot,remove);armed=-1;
                message=accepted?"Saving outfit slot...":"Slot could not be saved. Reopen Avatar to check it.";
                if(accepted)loaderUiSounds().outcome(false,false,true,now);
            }
        }
        const float dt=std::max(.001f,std::min(.1f,float(now-then)*.000001f));then=now;
#ifdef __vita__
        renderDt=std::min(.1f,renderDt+dt);
        if(!redraw.due(now,pad.buttons||wasTouch,unsigned(selected),backend.busy())) {
            loaderMusicTick();sceKernelDelayThread(redraw.pollDelay(now));continue;
        }
        const unsigned texture=backend.render(renderDt,0,0,false);renderDt=0;
#else
        const unsigned texture=backend.render(dt,0,0,false);
#endif
        loaderFrame();drawChrome(PageAvatar,settings,"Avatar / Outfits & history");
        uiText(12,60,"Outfits & history",kUiText,28,920,true);
        uiText(12,96,"Six outfit slots stay on this Vita. Undo history lasts until you leave Avatar.",kUiMuted,16,920);
        avatarDrawRenderTexture(texture,{20,120,304,304});
        for(size_t i=0;i<8;++i) {
            const auto box=VitaAvatar::Editor::outfitTile(i);
            uiRoundRect(box.x,box.y,box.w,box.h,7,i==selected?kUiPanelSelected:kUiPanel);
            const std::string title=i==0?"Undo last change":i==1?"Redo change":"Outfit "+std::to_string(i-1);
            const bool available=i==0?backend.canUndo():i==1?backend.canRedo():backend.outfitAvailable(unsigned(i-2));
            uiTextEllipsized(box.x+12,box.y+9,title,kUiText,18,box.w-24,i==selected);
            const bool problem=i>=2&&backend.outfitNeedsAttention(unsigned(i-2));
            uiText(box.x+12,box.y+38,problem?"Needs attention":available?"Ready":i<2?"Unavailable":"Empty",kUiMuted,16,box.w-24);
            if(i==selected)uiFocusFrame({box.x,box.y,box.w,box.h},Design::focus);
        }
        hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Wear / apply"},{RBX::SDLK_GAMEPAD_BUTTONX,"Save slot"},
            {RBX::SDLK_GAMEPAD_BUTTONY,"Delete slot"},{RBX::SDLK_GAMEPAD_BUTTONSTART,"Import help"},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}},14);
        backend.present();
    }
}

template<class Sampler>
std::string showAvatarEditor(Sampler& sampler,const RuntimeSettings& settings,VitaAvatar::EditorBackend& backend,LoaderInputHints& hints,LoaderPage& exitPage)
{
    exitPage=PageHome;
    drawLoadingCard(PageAvatar,settings,"YOUR AVATAR","Preparing your offline wardrobe...");
    if(!backend.open(avatarLegacyProfile(settings)))return backend.status();
    VitaAccount::connector().refreshAvatar();
    configureLoaderMusic(settings,true);
    struct Close { VitaAvatar::EditorBackend& backend; ~Close() { backend.close(); } } close{backend};
    VitaAvatar::Editor::Model menu;
    menu.build(backend.catalog(),backend.testing(),backend.selection());
    std::array<LoaderFileTexture,6> thumbnails;
    std::array<std::string,6> thumbnailPaths;
    std::array<bool,6> thumbnailAttempted{};
    // Cards display artwork within 140px. Decode a bounded, high-resolution
    // thumbnail off the input/render thread, retaining the original source.
    for(auto& texture:thumbnails) texture.setMaxSide(256);
    size_t lastTab=~size_t(0),lastPage=~size_t(0),lastSavedRevision=~size_t(0);
    unsigned previous=~0u;
    uint64_t repeat=0,then=sceKernelGetSystemTimeWide();
    bool waitingRelease=true,waitingPreviewRelease=true,leaving=false,categoryFocus=false,paletteOpen=false;
    size_t paletteSelected=0,palettePart=0;
    const auto& palette=avatarColorPalette();
    SceTouchPanelInfo panel={};
    sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
    SceTouchData initialTouch={};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initialTouch,1);
    bool wasTouch=initialTouch.reportNum!=0;
    Design::StickNavigation stick;
    VitaAvatar::PreviewGesture previewGesture;
    std::deque<SceTouchData> touches;
    std::string localMessage;
#ifdef __vita__
    LoaderRenderCadence redraw;
    float renderDt=0;
    VitaAvatar::WorkProfile workProfile;
#endif
    while(true)
    {
        VitaAccount::connector().tick([&](const std::string& bytes){return backend.applyAccountProfile(bytes);},!backend.busy());
        const uint64_t now=sceKernelGetSystemTimeWide();
        const float dt=std::max(.001f,std::min(.1f,float(now-then)*.000001f)); then=now;
        SceCtrlData pad={}; SceTouchData rear={};
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);
        applyLoaderStick(pad,stick);
        menu.syncSaved(backend.selection(),backend.catalog(),backend.testing());
        const size_t oldTab=menu.index(),oldSelected=menu.tab().selected,oldPalette=paletteSelected;
        const bool oldCategoryFocus=categoryFocus,oldPaletteOpen=paletteOpen;
        unsigned pressed=pad.buttons&~previous;
        const unsigned directions=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        const unsigned oldDirections=previous&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        if(directions && (directions!=oldDirections || now>=repeat))
        { pressed|=directions; repeat=now+(directions!=oldDirections?240000:85000); }
        if(!directions) repeat=0;
        previous=pad.buttons;
        float orbit=avatarStick(pad.rx,settings.stickDeadzone),zoom=avatarStick(pad.ry,settings.stickDeadzone);
        if(waitingRelease)
        {
            // A drifting preview stick must not suppress Back or menu buttons.
            if(!pad.buttons) waitingRelease=false;
            pressed=0; orbit=zoom=0;
        }
        if(waitingPreviewRelease)
        {
            if(!avatarStick(pad.rx,settings.stickDeadzone) && !avatarStick(pad.ry,settings.stickDeadzone)) waitingPreviewRelease=false;
            orbit=zoom=0;
        }
        int touchTab=-1;
        bool touchActivate=false,touchSearch=false;
        LoaderPage touchedPage=PageAvatar;
        for(const SceTouchData& sample:touches)
        {
            const bool down=sample.reportNum!=0;
            const float x=down?(sample.report[0].x-panel.minAaX)*960.f/
                std::max(1,int(panel.maxAaX-panel.minAaX)):0.f;
            const float y=down?(sample.report[0].y-panel.minAaY)*544.f/
                std::max(1,int(panel.maxAaY-panel.minAaY)):0.f;
            if(previewGesture.active())
            {
                previewGesture.sample(down,x,y);
                wasTouch=down;
                continue;
            }
            const auto previewBox=VitaAvatar::Editor::preview;
            if(down && !wasTouch && !waitingRelease && !leaving &&
                x>=previewBox.x && x<previewBox.x+previewBox.w &&
                y>=previewBox.y && y<previewBox.y+previewBox.h)
            {
                previewGesture.begin(x,y);
                wasTouch=true;
                continue;
            }
            if(sample.reportNum && !wasTouch && !waitingRelease && !leaving)
            {
                if(menu.tab().kind==VitaAvatar::Editor::Kind::Items && Design::Rect{812,60,136,44}.contains(x,y)){
                    touchSearch=true;wasTouch=true;continue;
                }
                if(y>=Design::tabsTop)
                {
                    touchedPage=loaderPageAtPosition(x);
                    break;
                }
                if (const unsigned touchMask=loaderControlMask(hints.hit(x,y)))
                {
                    pressed|=touchMask;
                    wasTouch=true;
                    continue;
                }
                if(paletteOpen) {
                    for(size_t i=0;i<palette.size();++i)
                        if(VitaAvatar::Editor::paletteSwatch(i).contains(x,y)) {
                            paletteSelected=i; touchActivate=true; break;
                        }
                    wasTouch=down;
                    continue;
                }
                touchTab=VitaAvatar::Editor::categoryHit(x,y,menu.index(),menu.count());
                if(VitaAvatar::Editor::categoryArrow(false).contains(x,y))
                    touchTab=int(VitaAvatar::Editor::categoryPage(menu.index(),menu.count(),-1));
                else if(VitaAvatar::Editor::categoryArrow(true).contains(x,y))
                    touchTab=int(VitaAvatar::Editor::categoryPage(menu.index(),menu.count(),1));
                if(touchTab>=0) { /* Category selection is handled below. */ }
                else if(menu.tab().kind==VitaAvatar::Editor::Kind::Colors)
                {
                    for(size_t i=0;i<6;++i)
                    {
                        const auto box=VitaAvatar::Editor::colorRow(i);
                        if(x>=box.x && x<box.x+box.w && y>=box.y && y<box.y+box.h)
                        {
                            touchActivate=true;
                            menu.tab().selected=i;
                            break;
                        }
                    }
                }
                else
                {
                    auto& current=menu.tab();
                    const size_t base=(current.selected/current.pageSize())*current.pageSize();
                    for(size_t i=0;i<current.pageSize() && base+i<current.count();++i)
                    {
                        const auto box=VitaAvatar::Editor::tile(i,false);
                        if(x>=box.x && x<box.x+box.w && y>=box.y && y<box.y+box.h)
                        {
                            touchActivate=true;
                            current.selected=base+i;
                            break;
                        }
                    }
                }
            }
            wasTouch=sample.reportNum!=0;
        }
        previewGesture.consume(dt,orbit,zoom);
        // Main tabs use the same L/R/SELECT controls throughout the loader.
        if(!leaving && (pressed&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SELECT)))
        {
            touchedPage=loaderAdjacentPage(PageAvatar,(pressed&SCE_CTRL_LTRIGGER)?-1:1);
            paletteOpen=false;
            pressed=0;
        }
        if(touchedPage!=PageAvatar)
        {
            loaderUiSounds().outcome(true,false,false,now);
            paletteOpen=false;
            exitPage=touchedPage;
            leaving=true;
            localMessage.clear();
        }
        if(touchTab>=0) { menu.selectTab(size_t(touchTab)); categoryFocus=false; pressed=0; }
        if(touchActivate) pressed|=SCE_CTRL_CROSS;
        if(paletteOpen && (pressed&SCE_CTRL_CIRCLE)) {
            paletteOpen=false; pressed=0;
        }
        if(paletteOpen) {
            paletteSelected=VitaAvatar::Editor::paletteMove(paletteSelected,palette.size(),
                (pressed&SCE_CTRL_RIGHT)?1:((pressed&SCE_CTRL_LEFT)?-1:0),
                (pressed&SCE_CTRL_DOWN)?1:((pressed&SCE_CTRL_UP)?-1:0));
            if((pressed&SCE_CTRL_CROSS) && !palette.empty()) {
                auto chosen=backend.selection();
                chosen.colors[palettePart]=palette[paletteSelected];
                chosen.enabled=true;chosen.customColors=true;
                if(chosen!=backend.selection() && !backend.request(chosen)) localMessage=backend.status();
                paletteOpen=false;
            }
            pressed=0;
        }
        if(pressed&SCE_CTRL_CIRCLE)
        {
            exitPage=PageHome;
            if(!backend.busy()) return backend.exitMessage();
            if(leaving) return "Left Avatar. Reopen it to check the last saved outfit.";
            leaving=true;
            localMessage="Finishing your outfit... Press Back again to leave now.";
        }
        if(leaving) {pressed=0;orbit=zoom=0;previewGesture.cancel();}
        if(categoryFocus)
        {
            if(pressed&SCE_CTRL_UP) menu.adjacent(-1);
            if(pressed&SCE_CTRL_DOWN) menu.adjacent(1);
            if(pressed&(SCE_CTRL_RIGHT|SCE_CTRL_CROSS)) categoryFocus=false;
            pressed&=~(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS);
        }
        else if((pressed&SCE_CTRL_LEFT) && menu.tab().selected%2==0)
        { categoryFocus=true; pressed&=~SCE_CTRL_LEFT; }
        auto& tab=menu.tab();
        const int dx=(pressed&SCE_CTRL_RIGHT)?1:((pressed&SCE_CTRL_LEFT)?-1:0);
        const int dy=(pressed&SCE_CTRL_DOWN)?1:((pressed&SCE_CTRL_UP)?-1:0);
        menu.move(dx,dy);
        bool change=false;
        if(tab.kind==VitaAvatar::Editor::Kind::Items && (touchSearch || (categoryFocus && (pressed&SCE_CTRL_SQUARE)))) {
            const std::string query=tab.query.empty()?promptText(u"Search this wardrobe category",{},48):std::string();
            tab.filterItems(query);lastPage=~size_t(0);pressed=0;waitingRelease=waitingPreviewRelease=true;
            then=sceKernelGetSystemTimeWide();
        }
        VitaAvatar::Profile selected=backend.selection();
        std::string fitId;
        if(const auto* item=tab.focusedItem())
            if(VitaAvatar::accessoryCategory(item->category) && std::find(selected.accessories.begin(),selected.accessories.end(),item->id)!=selected.accessories.end())fitId=item->id;
        if(const auto* saved=tab.focusedSaved())if(saved->slot==6&&saved->available)fitId=saved->id;
        if(pressed&SCE_CTRL_START) {
            if(fitId.empty() && (backend.feedback()==VitaAvatar::EditorBackend::Feedback::Error || backend.feedback()==VitaAvatar::EditorBackend::Feedback::Warning))
                showAvatarImportHelp(sampler,settings,hints,backend.status());
            else if(!fitId.empty())showAccessoryFit(sampler,settings,backend,hints,fitId);
            else showAvatarOutfits(sampler,settings,backend,hints);
            waitingRelease=waitingPreviewRelease=true;pressed=0;then=sceKernelGetSystemTimeWide();
        }
        if(tab.kind==VitaAvatar::Editor::Kind::Colors && (pressed&SCE_CTRL_CROSS))
        {
            palettePart=tab.selected;
            paletteSelected=0;
            for(size_t i=0;i<palette.size();++i)
                if(palette[i]==selected.colors[palettePart]) {paletteSelected=i;break;}
            paletteOpen=!palette.empty();
        }
        else if(pressed&SCE_CTRL_CROSS)
        {
            if(tab.kind==VitaAvatar::Editor::Kind::Presets)
            { avatarSetPreset(selected,unsigned(tab.selected)); change=true; }
            else if(tab.kind==VitaAvatar::Editor::Kind::Items)
            {
                if(tab.selected==0)
                { VitaAvatar::Editor::removeCategory(selected,tab.category,backend.catalog()); change=true; }
                else if(tab.category==VitaAvatar::Category::Face && tab.selected==1)
                { selected.slots[0]=VitaAvatar::noFaceId(); selected.enabled=true; change=true; }
                else if(const auto* item=tab.focusedItem())
                {
                    if(!item->browseable(backend.testing())) {
                        localMessage="Original preview only. This item is not available to wear on this client.";
                    } else {
                    // Choosing another hairstyle replaces the current hair.
                    // Hats and other accessories retain their independent slots.
                    if(item->category==VitaAvatar::Category::Hair && !VitaAvatar::Editor::isEquipped(selected,*item))
                        VitaAvatar::Editor::removeCategory(selected,VitaAvatar::Category::Hair,backend.catalog());
                    change=VitaAvatar::equip(selected,*item,backend.testing());
                    if(!change) localMessage="Remove an accessory before adding another.";
                    }
                }
            }
        }
        if((pressed&SCE_CTRL_SQUARE) && !categoryFocus && tab.kind==VitaAvatar::Editor::Kind::Items)
        { VitaAvatar::Editor::removeCategory(selected,tab.category,backend.catalog()); change=true; }
        if((pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE)) && tab.kind==VitaAvatar::Editor::Kind::Saved)
            if(const auto* entry=tab.focusedSaved()) change=VitaAvatar::Editor::removeSavedItem(selected,*entry);
        change=change && selected!=backend.selection();
        if(change)
        {
            localMessage.clear();
            if(!backend.request(selected)) localMessage=backend.status();
            else loaderUiSounds().outcome(false,false,true,now);
        }
        loaderUiSounds().outcome(false,menu.index()!=oldTab||menu.tab().selected!=oldSelected||categoryFocus!=oldCategoryFocus||paletteSelected!=oldPalette,
            paletteOpen!=oldPaletteOpen,now);
        const bool resetView=(pressed&SCE_CTRL_TRIANGLE)!=0;
#ifdef __vita__
        renderDt=std::min(.1f,renderDt+dt);
        const bool interaction=pad.buttons||wasTouch||std::fabs(orbit)>.001f||std::fabs(zoom)>.001f||resetView||change;
        if(!redraw.due(now,interaction,unsigned(menu.index()),backend.busy())){
            loaderMusicTick();sceKernelDelayThread(redraw.pollDelay(now));continue;
        }
        const auto workBegin=VitaAvatar::WorkProfile::stamp(settings.performanceLogging);
#endif
        // Keep the user's chosen view until they rotate it themselves.
        backend.setPreviewArea(avatarPreviewArea(tab));
#ifdef __vita__
        const unsigned previewTexture=backend.render(renderDt,orbit,zoom,resetView);renderDt=0;
        const auto workPreview=VitaAvatar::WorkProfile::stamp(settings.performanceLogging);
#else
        const unsigned previewTexture=backend.render(dt,orbit,zoom,resetView);
#endif
        menu.syncSaved(backend.selection(),backend.catalog(),backend.testing());
        loaderFrame();
        const std::string feedback=localMessage.empty()?backend.status():localMessage;
        const auto feedbackState=backend.feedback();
        const bool problem=feedbackState==VitaAvatar::EditorBackend::Feedback::Warning ||
            feedbackState==VitaAvatar::EditorBackend::Feedback::Error;
        // A single visible status in the shared header leaves the preview and
        // 44px action row uncluttered. Detailed errors stay beside the character.
        const std::string feedbackTitle=avatarFeedbackTitle(feedbackState,backend.selection().enabled);
        const std::string feedbackDetail=(problem || feedbackState==VitaAvatar::EditorBackend::Feedback::Updating || !localMessage.empty()) ? feedback : avatarCategoryHelp(tab,categoryFocus);
        drawChrome(PageAvatar,settings,feedbackDetail==feedbackTitle?feedbackTitle:feedbackTitle+" / "+feedbackDetail);
        const auto& preview=VitaAvatar::Editor::preview;
        uiRoundRect(preview.x,preview.y,preview.w,preview.h,10,kUiPanel);
        avatarDrawRenderTexture(previewTexture,VitaAvatar::Editor::previewImage);
        if(!previewTexture) {
            const std::string label=feedbackState==VitaAvatar::EditorBackend::Feedback::Error?
                "Preview unavailable":"Loading avatar";
            const auto& image=VitaAvatar::Editor::previewImage;
            const float pixels=Typography::pixels(17);
            const float width=loaderFont().measure(label,pixels,120,true);
            uiText(image.x+(image.w-width)*.5f,
                image.y+(image.h-pixels)*.5f,label,kUiMuted,pixels,width,true);
        }

        uiTextEllipsized(20,390,feedbackState==VitaAvatar::EditorBackend::Feedback::Updating?"Loading your outfit...":"Your saved outfit",kUiText,20,304,true);
        

        if(paletteOpen) {
            static const char* parts[]={"Head","Torso","Left arm","Right arm","Left leg","Right leg"};
            drawChrome(PageAvatar,settings,std::string(parts[palettePart])+" / "+
                avatarColorName(int(palette[paletteSelected]))+" / Confirm: apply / Back: cancel");
            uiRoundRect(340,60,608,380,10,kUiPanel);
            uiText(348,70,std::string(parts[palettePart])+" color",kUiText,20,400,true);
            for(size_t i=0;i<palette.size();++i) {
                const auto box=VitaAvatar::Editor::paletteSwatch(i);
                uiRoundRect(box.x,box.y,box.w,box.h,4,kUiLine);
                uiRoundRect(box.x+1,box.y+1,box.w-2,box.h-2,3,avatarUiColor(int(palette[i])));
                if(i==paletteSelected) uiFocusFrame({box.x,box.y,box.w,box.h},Design::focus,4);
            }
            hints.draw(Design::margin,Design::promptsY,936,{
                {RBX::SDLK_GAMEPAD_BUTTONA,"Apply color"},
                {RBX::SDLK_GAMEPAD_BUTTONB,"Cancel"},
                {RBX::SDLK_GAMEPAD_BUTTONL1,"Tabs",RBX::SDLK_GAMEPAD_BUTTONR1}},13);
#ifdef __vita__
            const auto workUi=VitaAvatar::WorkProfile::stamp(settings.performanceLogging);
#endif
            backend.present();
#ifdef __vita__
            workProfile.frame(workBegin,workPreview,workUi,VitaAvatar::WorkProfile::stamp(settings.performanceLogging));
#endif
            continue;
        }

        const size_t categoryBase=VitaAvatar::Editor::categoryBase(menu.index());

        for(size_t visible=0;visible<VitaAvatar::Editor::categoryPageSize && categoryBase+visible<menu.count();++visible)
        {
            const size_t i=categoryBase+visible;
            const auto box=VitaAvatar::Editor::categoryRow(visible);
            const bool current=i==menu.index();
            uiRoundRect(box.x,box.y,box.w,box.h,8,current?kUiPanelSelected:kUiPanel);
            if(current) uiRoundRect(box.x+4,box.y+8,3,box.h-16,1.5f,Design::focus);
            uiTextEllipsized(box.x+11,box.y+13,menu.tabAt(i).title,
                current?kUiText:kUiMuted,18,box.w-19,current);
            if(current && categoryFocus) uiFocusFrame({box.x,box.y,box.w,box.h},Design::focus,Design::controlRadius);
        }
        for(unsigned next=0;next<2;++next)
        {
            const auto box=VitaAvatar::Editor::categoryArrow(next!=0);
            const bool available=VitaAvatar::Editor::categoryPage(menu.index(),menu.count(),next?1:-1)!=menu.index();
            uiRoundRect(box.x,box.y,box.w,box.h,7,available?Design::focusFill:Design::subtle);
            const char* arrow=next?">":"<";
            const auto measured=loaderFont().measure(arrow,22,120,true);
            uiText(box.x+(box.w-measured)/2,box.y+(box.h-22)/2,arrow,available?kUiToolbar:0x8694a0,22,measured,true);
        }
        uiTextEllipsized(520,60,tab.title,kUiText,Design::title,290,true);

        const auto& outfit=backend.selection();
        std::string count;
        if(tab.kind==VitaAvatar::Editor::Kind::Items)
            count=std::to_string(tab.items.size())+" ITEMS";
        else if(tab.kind==VitaAvatar::Editor::Kind::Saved)
            count=std::to_string(tab.saved.size())+" SAVED";
        if(!count.empty()) uiTextEllipsized(520,Design::headingCaptionY,count,kUiMuted,16,128,true);
        if(tab.kind==VitaAvatar::Editor::Kind::Items){
            uiRoundRect(812,60,136,44,8,Design::surface);
            uiText(824,73,tab.query.empty()?"Search":"Clear search",Design::ink,18,108,true);
        }
        if(tab.kind==VitaAvatar::Editor::Kind::Colors)
        {
            static const char* names[]={"Head","Torso","Left arm","Right arm","Left leg","Right leg"};
            for(unsigned i=0;i<6;++i)
            {
                const auto box=VitaAvatar::Editor::colorRow(i);
                const bool focus=i==tab.selected && !categoryFocus;
                uiRoundRect(box.x,box.y,box.w,box.h,10,focus?kUiPanelSelected:kUiPanel);
                uiDisc(box.x+42,box.y+52,25,avatarUiColor(int(outfit.colors[i])));
                uiText(box.x+76,box.y+28,names[i],kUiText,18,box.w-88,focus);
                uiTextEllipsized(box.x+76,box.y+54,avatarColorName(int(outfit.colors[i])),kUiMuted,16,box.w-88);
                if(focus) uiFocusFrame({box.x,box.y,box.w,box.h},Design::focus);
            }
        }
        else
        {
            const size_t pageSize=tab.pageSize();
            const size_t base=(tab.selected/pageSize)*pageSize;
            if(lastTab!=menu.index() || lastPage!=base ||
               (tab.kind==VitaAvatar::Editor::Kind::Saved && lastSavedRevision!=menu.savedRevision()))
            {
                for(unsigned i=0;i<6;++i) { thumbnails[i].reset(); thumbnailPaths[i].clear(); thumbnailAttempted[i]=false; }
                lastTab=menu.index(); lastPage=base; lastSavedRevision=menu.savedRevision();
                for(unsigned i=0;i<pageSize && base+i<tab.count();++i)
                    if(const auto* item=tab.itemAt(base+i))
                        thumbnailPaths[i]=avatarArtworkPath(*item,backend);
            }
            for(unsigned i=0;i<pageSize;++i)
                if(!thumbnailPaths[i].empty() && !thumbnailAttempted[i])
                { thumbnailAttempted[i]=true; thumbnails[i].loadAsync(thumbnailPaths[i],backend.artworkReader(thumbnailPaths[i])); break; }
            static const char* presetNames[]={"Game appearance","Classic noob","Red and black","Blue and white"};
            for(unsigned i=0;i<pageSize && base+i<tab.count();++i)
            {
                const size_t index=base+i;
                const bool compact=tab.kind==VitaAvatar::Editor::Kind::Presets;
                const auto box=VitaAvatar::Editor::tile(i,false);
                const bool focus=index==tab.selected && !categoryFocus;
                uiRoundRect(box.x,box.y,box.w,box.h,10,focus?kUiPanelSelected:kUiPanel);
                bool equipped=false;
                std::string name;
                const VitaAvatar::Item* item=tab.itemAt(index);
                if(compact)
                {
                    name=presetNames[index];
                    if(!index) equipped=!outfit.enabled;
                    else
                    {
                        const AvatarColors colors=avatarColors(unsigned(index));
                        const unsigned values[]={colors.head,colors.torso,colors.leftArm,colors.rightArm,colors.leftLeg,colors.rightLeg};
                        for(unsigned color=0;color<6;++color) {
                            const float swatchX=box.x+14+float(color)*30;
                            uiRoundRect(swatchX,box.y+50,24,24,4,kUiLine);
                            uiRoundRect(swatchX+1,box.y+51,22,22,3,values[color]);
                        }
                        VitaAvatar::Profile candidate=outfit; avatarSetPreset(candidate,unsigned(index));
                        equipped=outfit.enabled && outfit.customColors && outfit.colors==candidate.colors;
                    }
                    if(!index) uiText(box.x+12,box.y+50,"Use game appearance",kUiMuted,18,box.w-24);
                    uiTextEllipsized(box.x+12,box.y+box.h-32,name,kUiText,20,box.w-24,focus);
                }
                else
                {
                    VitaAvatar::Category category=item?item->category:tab.category;
                    if(tab.kind==VitaAvatar::Editor::Kind::Saved)
                    {
                        if(index<tab.saved.size())
                        {
                            const auto& entry=tab.saved[index];
                            name=VitaAvatar::Editor::savedItemName(entry);
                            category=entry.item?entry.item->category:VitaAvatar::Category::Invalid;
                            equipped=entry.available && outfit.enabled;
                        }
                        else name="No saved items";
                    }
                    else if(index<tab.leading())
                    {
                        name=tab.category==VitaAvatar::Category::Face?(index==0?"Default face":"No face"):
                            (tab.category==VitaAvatar::Category::Head?"Default head":"None");
                        if(tab.category==VitaAvatar::Category::Face)
                            equipped=outfit.enabled && outfit.slots[0]==
                                (index==0?std::string():std::string(VitaAvatar::noFaceId()));
                    }
                    else if(item)
                    {
                        name=item->name;
                        equipped=VitaAvatar::Editor::isEquipped(outfit,*item);
                    }
                    // Cards show the selected item. The character belongs in the
                    // large preview, where it can be inspected and rotated.
                    bool drawn=false;
                    if(item && thumbnailAttempted[i])
                    {
                        drawn=avatarDrawItemArtwork(thumbnails[i],thumbnailPaths[i],category,box,backend.artworkReader(thumbnailPaths[i]));
                    }
                    if(!drawn)
                    {
                        const char* placeholder=item ? "Image unavailable" :
                            tab.kind==VitaAvatar::Editor::Kind::Saved ? "Saved outfit" :
                            (tab.category==VitaAvatar::Category::Face && index==1 ? "No face" : "Default");
                        uiText(box.x+16,box.y+38,placeholder,kUiMuted,16,box.w-32,true);
                        const char* explanation=item ? "" :
                            tab.kind==VitaAvatar::Editor::Kind::Saved ?
                                (index<tab.saved.size() ? "Missing item; removable" : "Your equipped items") :
                            (tab.category==VitaAvatar::Category::Face && index==1 ? "Clear the face graphic" : "Reset this category");
                        uiTextEllipsized(box.x+16,box.y+65,explanation,kUiMuted,16,box.w-32);
                    }
                    uiTextEllipsized(box.x+12,box.y+box.h-32,name,kUiText,20,box.w-24,focus);
                }
                const bool savedInactive=tab.kind==VitaAvatar::Editor::Kind::Saved &&
                    index<tab.saved.size() && tab.saved[index].available && !outfit.enabled;
                const bool previewOnly=item&&!item->browseable(backend.testing());
                if(equipped || savedInactive || previewOnly)
                {
                    const char* badge=previewOnly?"PREVIEW":savedInactive?"SAVED":
                        (compact?"ACTIVE":(backend.busy()?"SELECTED":"EQUIPPED"));
                    const float badgeWidth=loaderFont().measure(badge,18,120,true)+16;
                    const float badgeX=box.x+box.w-badgeWidth-8;
                    const unsigned badgeColor=(savedInactive||previewOnly)?kUiMuted:0x087a48;
                    uiRoundRect(badgeX,box.y+6,badgeWidth,26,4,badgeColor);
                    uiText(badgeX+8,box.y+10,badge,Design::badgeInk(badgeColor),18,badgeWidth-16,true);
                }
                if(focus) uiFocusFrame({box.x,box.y,box.w,box.h},Design::focus);
            }
            if(tab.count()>pageSize)
                uiText(650,88,"Page "+std::to_string(base/pageSize+1)+" / "+
                    std::to_string((tab.count()+pageSize-1)/pageSize),kUiMuted,16,152);
        }
        if(tab.kind==VitaAvatar::Editor::Kind::Items && !categoryFocus)
            hints.draw(Design::margin,Design::promptsY,936,{
                {RBX::SDLK_GAMEPAD_BUTTONA,avatarPrimaryAction(tab,outfit,false)},
                {RBX::SDLK_GAMEPAD_BUTTONX,"Clear category"},
                {RBX::SDLK_GAMEPAD_BUTTONSTART,fitId.empty()?(problem?"Outfit details":"Outfits & undo"):"Adjust fit"},
                {RBX::SDLK_GAMEPAD_BUTTONY,"Reset view"},
                {RBX::SDLK_GAMEPAD_BUTTONL1,"Tabs",RBX::SDLK_GAMEPAD_BUTTONR1},
                {RBX::SDLK_GAMEPAD_BUTTONB,"Done"}},13);
        else if(tab.kind==VitaAvatar::Editor::Kind::Items && categoryFocus)
            hints.draw(Design::margin,Design::promptsY,936,{
                {RBX::SDLK_GAMEPAD_BUTTONA,"Open items"},{RBX::SDLK_GAMEPAD_BUTTONX,"Search"},
                {RBX::SDLK_GAMEPAD_BUTTONL1,"Tabs",RBX::SDLK_GAMEPAD_BUTTONR1},{RBX::SDLK_GAMEPAD_BUTTONB,"Done"}},13);
        else if(tab.kind==VitaAvatar::Editor::Kind::Saved && !tab.focusedSaved() && !categoryFocus)
            hints.draw(Design::margin,Design::promptsY,936,{
                {RBX::SDLK_GAMEPAD_BUTTONSTART,"Outfits & undo"},
                {RBX::SDLK_GAMEPAD_BUTTONY,"Reset view"},
                {RBX::SDLK_GAMEPAD_BUTTONL1,"Tabs",RBX::SDLK_GAMEPAD_BUTTONR1},
                {RBX::SDLK_GAMEPAD_BUTTONB,"Done"}},13);
        else
            hints.draw(Design::margin,Design::promptsY,936,{
                {RBX::SDLK_GAMEPAD_BUTTONA,avatarPrimaryAction(tab,outfit,categoryFocus)},
                {RBX::SDLK_GAMEPAD_BUTTONSTART,fitId.empty()?(problem?"Outfit details":"Outfits & undo"):"Adjust fit"},
                {RBX::SDLK_GAMEPAD_BUTTONY,"Reset view"},
                {RBX::SDLK_GAMEPAD_BUTTONL1,"Tabs",RBX::SDLK_GAMEPAD_BUTTONR1},
                {RBX::SDLK_GAMEPAD_BUTTONB,"Done"}},13);
#ifdef __vita__
        const auto workUi=VitaAvatar::WorkProfile::stamp(settings.performanceLogging);
#endif
        backend.present();
#ifdef __vita__
        workProfile.frame(workBegin,workPreview,workUi,VitaAvatar::WorkProfile::stamp(settings.performanceLogging));
#endif
        if(leaving && !backend.busy()) return backend.exitMessage();
    }
}
