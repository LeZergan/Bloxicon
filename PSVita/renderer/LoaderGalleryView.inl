// Photos use the same shell/actions as Details, with a widescreen media deck.
inline void drawGameGalleryContent(const std::string& title,const std::string& fallback,
    const Gallery::Manifest& manifest,size_t selected,const std::string& imagePath,const std::string& status,bool pending,
    LoaderFileTexture& hero,LoaderFileTexture& icon,std::array<LoaderFileTexture,Gallery::visible>& tiles)
{
    uiRoundRect(12,60,584,328,Design::cornerRadius,Design::subtle);
    const bool rendered=!imagePath.empty()&&hero.loadAsync(imagePath)&&hero.drawFit(imagePath,20,68,568,312,Design::imageRadius);
    const bool decoding=!imagePath.empty()&&!hero.asyncResolved();
    if(!rendered){
        const bool fallbackDrawn=!fallback.empty()&&icon.loadAsync(fallback)&&icon.drawFit(fallback,20,68,568,312,Design::imageRadius);
        if(!fallbackDrawn){
            const auto lines=Geometry::titleLines(title,536,[](const std::string& value){return loaderFont().measure(value,28,120,true);});
            for(unsigned i=0;i<2&&!lines[i].empty();++i)uiText(36,188+float(i)*34,lines[i],kUiText,28,536,true);
        }
        uiRoundRect(28,336,552,36,Design::controlRadius,Design::surface);
        uiText(40,344,manifest.pending&&manifest.photos.empty()?"Preparing screenshots...":pending||decoding?"Loading screenshot...":"Screenshot unavailable",kUiMuted,18,528);
    }
    uiRoundRect(604,60,344,328,Design::cornerRadius,Design::surface);
    uiText(616,72,"Photos",kUiText,28,320,true);
    uiText(616,108,"Current online game screenshots",kUiMuted,16,320);
    const std::string caption=!status.empty()?status:pending?"Loading...":manifest.pending&&manifest.photos.empty()?"Screenshots are being prepared":manifest.photos.empty()?"No screenshots available":
        "Photo "+std::to_string(selected+1)+" / "+std::to_string(manifest.photos.size());
    uiTextEllipsized(616,132,caption,status.empty()?kUiMuted:kUiOrange,16,320);
    const auto first=Gallery::pageBase(selected,manifest.photos.size());
    for(size_t i=0;i<Gallery::visible&&first+i<manifest.photos.size();++i){const size_t at=first+i;const auto box=Gallery::row(i);const bool active=at==selected;
        uiRoundRect(box.x,box.y,box.w,box.h,Design::controlRadius,active?Design::focusFill:Design::subtle);
        // Selected media is distinct from the action that Confirm activates.
        if(active)uiRect(box.x+104,box.y+box.h-3,box.w-112,3,Design::focus);
        const auto path=galleryImagePath(manifest.universeId,manifest.photos[at].id);
        const bool tileDrawn=tiles[i].loadAsync(path)&&tiles[i].drawFit(path,box.x+8,box.y+8,88,52,Design::imageRadius);
        if(!tileDrawn){
            uiRoundRect(box.x+8,box.y+8,88,52,Design::imageRadius,Design::surface);
            const auto number=std::to_string(at+1);const auto width=loaderFont().measure(number,24,120,true);
            uiText(box.x+52-width*.5f,box.y+22,number,kUiMuted,24,width,true);
        }
        uiText(box.x+104,box.y+10,"Photo "+std::to_string(at+1),active?Design::focus:kUiText,20,204,true);
        uiText(box.x+104,box.y+38,active?"Selected":"Select to view",kUiMuted,16,204);
    }
    if(manifest.photos.empty()&&!pending){
        const std::string help=manifest.pending?"Bloxicon is preparing these screenshots. Reopen Photos to check again. Your local game remains available.":
            "Screenshots are optional. Your local game remains available through the actions below.";
        const auto lines=Geometry::paragraphLines(help,320,6,
            [](const std::string& value){return loaderFont().measure(value,20);});
        for(size_t i=0;i<lines.size();++i)uiText(616,172+float(i)*28,lines[i],kUiText,20,320);
    }
}
