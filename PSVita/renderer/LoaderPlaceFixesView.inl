template<class Sampler>
void showPlaceFixes(Sampler& sampler,RuntimeSettings& settings) {
    const char* labels[]={"Image placeholders","Sound placeholders","Animation placeholders",
        "Loading assistance","Script error logs","Missing asset downloads"};
    const char* details[]={"Use the engine's plain texture when an image cannot load.",
        "Use short silent audio when a sound cannot load.","Use a stationary keyframe when an animation cannot load.",
        "Offer a choice on slow loads. Games never exit just because they take longer.",
        "Record setup script errors for troubleshooting.","Download supported missing classic assets before using a placeholder."};
    const unsigned flags[]={1,4,8,32,64,0};size_t selected=0;
    const size_t optionCount=sizeof(labels)/sizeof(labels[0]);
    LoaderInputHints hints;SceCtrlData pad={};SceTouchData rear={},initial={};std::deque<SceTouchData> touches;
    sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned previous=pad.buttons;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);
    bool wasTouch=initial.reportNum!=0;
    #ifdef __vita__
    LoaderRenderCadence redraw(100000);
#endif
    for(;;) {
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned pressed=pad.buttons&~previous;previous=pad.buttons;
        for(const auto& touch:touches) {
            if(touch.reportNum&&!wasTouch) {
                const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                const float y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                if(queueLoaderTab(x,y,PageSettings))return;
                for(size_t i=0;i<optionCount;++i)if(Design::Rect{12.f+float(i%2)*472,112.f+float(i/2)*84,464,76}.contains(x,y)) {
                    selected=i;pressed|=SCE_CTRL_CROSS;
                }
                pressed|=loaderControlMask(hints.hit(x,y));
            }wasTouch=touch.reportNum!=0;
        }
        if(pressed&SCE_CTRL_CIRCLE)return;
        if((pressed&SCE_CTRL_UP)&&selected)--selected;
        if((pressed&SCE_CTRL_DOWN)&&selected+1<optionCount)++selected;
        if(pressed&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))selected=selected<4?4:0;
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
            if(selected==optionCount-1)settings.autoAssetRepair=!settings.autoAssetRepair;else settings.placeFixes^=flags[selected];
            saveSettings(settings);RBX::VitaPlaceFixes::options().store(settings.placeFixes);
        }
        #ifdef __vita__
        if(!loaderMenuRedraw(redraw,pad,touches))continue;
#endif
        loaderFrame();drawChrome(PageSettings,settings,details[selected]);
        uiText(12,60,"Place assistance",kUiText,28,936,true);
        uiText(12,Design::headingCaptionY,"Shared options for every place. Missing original content cannot be recovered by a fallback.",kUiMuted,16,936);
        for(size_t i=0;i<optionCount;++i) {
            const Design::Rect b={12.f+float(i%2)*472,112.f+float(i/2)*84,464,76};const bool focused=i==selected;
            uiRoundRect(b.x,b.y,b.w,b.h,10,focused?kUiPanelSelected:kUiPanel);
            if(focused)uiFocusFrame(b);
            uiText(b.x+12,b.y+16,labels[i],kUiText,22,350,focused);
            uiTextEllipsized(b.x+12,b.y+44,details[i],kUiMuted,16,440);
            const bool on=i==optionCount-1?settings.autoAssetRepair:(settings.placeFixes&flags[i])!=0;
            uiText(b.x+388,b.y+13,on?"ON":"OFF",focused?Design::focus:kUiText,26,64,true);
        }
        hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Change"},
            {RBX::SDLK_GAMEPAD_DPADLEFT,"Adjust",RBX::SDLK_GAMEPAD_DPADRIGHT},
            {RBX::SDLK_GAMEPAD_BUTTONB,"Back"},{RBX::SDLK_GAMEPAD_BUTTONL2,"Sections",RBX::SDLK_GAMEPAD_BUTTONR2}},14);
        vglSwapBuffers(GL_FALSE);sceKernelDelayThread(8000);
    }
}
