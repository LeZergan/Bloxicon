// Shared native browsing components. Included after original font/image helpers.
// My games and Discover intentionally consume the same geometry and renderer.
struct GameCardText {
    std::vector<std::string> title;
    std::string detail;
    float titleSize=28;
};
inline GameCardText gameCardText(const std::string& name,const std::string& detail){
    GameCardText result;result.detail=loaderDisplayText(detail);
    const auto title=loaderDisplayText(detailTitle(name));
    for(;;){result.title=Geometry::paragraphLines(title,Design::cardTextWidth,4096,[&](const std::string& text){return loaderFont().measure(text,result.titleSize,120,true);});
        if(result.title.size()<=2||result.titleSize<=18)break;result.titleSize-=2;}
    if(result.title.empty())result.title.push_back("Game");
    return result;
}

inline void drawGameCard(size_t slot, const GameCardText& text, LoaderFileTexture& image,
    const std::string& imagePath, bool available, bool focused, const char* badge,
    unsigned badgeColor = Design::muted)
{
    const Design::Rect box = Design::libraryCard(slot);
    uiRoundRect(box.x,box.y,box.w,box.h,10,Design::surface);
    const float edge=box.h-2*Design::cardPadding;
    const Design::Rect art = {box.x + Design::cardPadding, box.y + Design::cardPadding, edge, edge};
    uiRoundRect(art.x, art.y, art.w, art.h, Design::imageRadius, Design::subtle);
    if (available)
        image.drawCover(imagePath, art.x, art.y, art.w, art.h, Design::imageRadius);
    else
    {
        // An honest, quiet fallback, not a generated substitute for game artwork.
        const std::string initial=text.title[0].empty()?"G":text.title[0].substr(0,1);
        const float width=loaderFont().measure(initial,32,120,true);
        uiText(art.x+(art.w-width)*.5f,art.y+(art.h-32)*.5f,initial,Design::muted,32,width,true);
    }
    const float x=art.x+art.w+12;
    const size_t visible=std::min(size_t(4),text.title.size());
    const size_t page=focused&&text.title.size()>4?size_t(sceKernelGetSystemTimeWide()/4000000)%((text.title.size()+3)/4):0;
    const float pitch=text.titleSize+2;
    const bool caption=visible<=2&&!text.detail.empty();
    const float height=float(visible)*pitch+(caption?26.f:0.f);
    const float top=box.y+std::max(6.f,(box.h-height)*.5f);
    for(size_t i=page*4;i<std::min(page*4+4,text.title.size());++i)
        uiText(x,top+float(i-page*4)*pitch,text.title[i],Design::ink,text.titleSize,Design::cardTextWidth,true);
    if(caption)uiTextEllipsized(x,top+float(visible)*pitch+4,text.detail,Design::muted,18,240);
    if(badge&&*badge&&visible<=2){
        const float width=std::min(Design::cardTextWidth,loaderFont().measure(badge,18,120,true)+16);
        const float y=box.y+box.h-28;
        uiRoundRect(box.x+box.w-width-10,y,width,22,Design::controlRadius,badgeColor);
        uiText(box.x+box.w-width-2,y+2,badge,Design::badgeInk(badgeColor),18,width-16,true);
    }
    if (focused) uiFocusFrame(box);
}
