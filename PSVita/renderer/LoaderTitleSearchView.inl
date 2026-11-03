// Controller/touch review of current online title results. No HTTP on the draw thread.
inline void drawTitleMatches(const TitleSearch::Result& result,size_t selected,bool pending,LoaderInputHints& hints){
    uiTextEllipsized(12,60,"Find game by title",Design::ink,Design::title,820,true);
    uiTextEllipsized(12,Design::headingCaptionY,"Search: "+detailDisplayText(result.query),Design::muted,16,800);
    uiText(856,72,std::to_string(result.candidates.empty()?0:selected+1)+" / "+std::to_string(result.candidates.size()),Design::muted,20,80,true);
    const auto first=TitleSearch::pageBase(selected,result.candidates.size());
    for(size_t slot=0;slot<TitleSearch::visible&&first+slot<result.candidates.size();++slot){
        const auto index=first+slot;const auto& item=result.candidates[index];const auto box=TitleSearch::rowRect(slot,result.candidates.size());
        uiRoundRect(box.x,box.y,box.w,box.h,10,index==selected?Design::focusFill:Design::surface);
        if(index==selected)uiRoundRect(16,box.y+6,4,box.h-12,2,Design::focus);
        const float y=box.y+(box.h>=120?6:0);
        uiTextEllipsized(28,y+2,detailDisplayText(item.name),Design::ink,22,568,index==selected);
        uiTextEllipsized(28,y+30,(item.creator.empty()?"Creator unavailable":detailDisplayText(item.creator))+" / Place "+std::to_string(item.placeId),Design::muted,18,568);
        if(box.h>=120){
            const auto lines=Geometry::paragraphLines(detailDisplayText(item.description.empty()?"No description supplied for this result.":item.description),568,
                std::min(size_t(8),size_t((box.h-64)/20)),[](const std::string& text){return loaderFont().measure(text,16);});
            for(size_t i=0;i<lines.size();++i)uiText(28,box.y+58+float(i)*20,lines[i],Design::muted,16,568);
        }
    }
    if(result.candidates.empty()){
        uiRoundRect(12,112,936,316,10,Design::surface);
        uiTextEllipsized(28,156,pending?"Searching online games...":result.error.empty()?"No results":result.error,Design::ink,22,896,true);
        uiText(28,198,"You can edit the title or go back at any time.",Design::muted,18,896);
    }
    uiTextEllipsized(12,424,"Choose matching name, creator and picture. Your installed place stays the same.",Design::muted,15,936);
    hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_DPADUP,"Select",RBX::SDLK_GAMEPAD_DPADDOWN},
        {RBX::SDLK_GAMEPAD_BUTTONA,"Use info"},{RBX::SDLK_GAMEPAD_BUTTONX,"Edit title"},
        {RBX::SDLK_GAMEPAD_BUTTONB,"Back"}},15);
}
template<class Sampler>
bool chooseTitleMatch(Sampler& sampler,const RuntimeSettings& settings,LoaderPage origin,TitleSearch::Result result,
    TitleSearch::Candidate& choice,std::string& query,bool startSearch=false){
    LoaderInputHints hints;size_t selected=0;unsigned previous=~0u;bool wasTouch=false,pending=startSearch;uint64_t nextRepeat=0;
    std::string queuedQuery=startSearch?result.query:std::string();uint64_t searchAt=0;
    LoaderAsyncTask<DetailsTaskResult> imageTask;LoaderFileTexture previewTexture;
    long long previewId=0;bool imagePending=false,imageRequested=false;uint64_t imageAt=0;std::string previewPath,previewError;
    Design::StickNavigation stick;LoaderAsyncTask<TitleSearch::Result> task;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);
    SceTouchData initial={};sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);wasTouch=initial.reportNum!=0;
    #ifdef __vita__
    LoaderRenderCadence redraw(100000);
#endif
    while(true){
        SceCtrlData pad={};SceTouchData rear={};std::deque<SceTouchData> touches;sampler.read(pad,touches,rear);
        pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,settings.acceptCircle);applyLoaderStick(pad,stick);
        unsigned pressed=pad.buttons&~previous;const auto now=sceKernelGetSystemTimeWide();
        const unsigned nav=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN);
        if(nav&&now>=nextRepeat){pressed|=nav;nextRepeat=now+(nav==(previous&nav)?140000:350000);}if(!nav)nextRepeat=0;
        previous=pad.buttons;bool touching=false;
        for(const auto& touch:touches){if(touch.reportNum&&!wasTouch){
            const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
            const float y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
            if(queueLoaderTab(x,y,origin))return false;
            const auto first=TitleSearch::pageBase(selected,result.candidates.size());
            for(size_t slot=0;slot<TitleSearch::visible&&first+slot<result.candidates.size();++slot)
                if(TitleSearch::rowRect(slot,result.candidates.size()).contains(x,y)){selected=first+slot;break;}
            pressed|=loaderControlMask(hints.hit(x,y));}touching=touch.reportNum!=0;}wasTouch=touching;
        if(pressed&SCE_CTRL_CIRCLE)return false;
        if((pressed&SCE_CTRL_UP)&&selected)--selected;
        if((pressed&SCE_CTRL_DOWN)&&selected+1<result.candidates.size())++selected;
        if((pressed&SCE_CTRL_CROSS)&&!result.candidates.empty()){choice=result.candidates[selected];query=result.query;return true;}
        if(pressed&SCE_CTRL_SQUARE){
            const auto entered=promptText(u"Find game by title",result.query,64);previous=entered.empty()?~0u:pad.buttons;
            if(!entered.empty()){
                task.cancel();result=TitleSearch::Result();result.query=entered;selected=0;
                queuedQuery=entered;pending=true;searchAt=0;
            }
        }
        if(pending&&queuedQuery.empty()){TitleSearch::Result completed;std::string error;const auto state=task.poll(completed,error);
            if(state==LoaderAsyncTask<TitleSearch::Result>::Ready){result=std::move(completed);pending=false;}
            else if(state==LoaderAsyncTask<TitleSearch::Result>::Failed||state==LoaderAsyncTask<TitleSearch::Result>::Cancelled){pending=false;result.error="Title search could not load. Edit the title to retry.";}}
        if(pending&&searchAt&&now-searchAt>20000000){task.cancel();pending=false;result.error="Search timed out. Edit the title to retry.";}
        const long long desired=result.candidates.empty()?0:result.candidates[selected].universeId;
        if(desired!=previewId){imageTask.cancel();imagePending=false;imageRequested=false;previewId=desired;previewPath.clear();previewError.clear();previewTexture.reset();imageAt=now;}
        if(imagePending){DetailsTaskResult value;std::string error;const auto state=imageTask.poll(value,error);
            if(state==LoaderAsyncTask<DetailsTaskResult>::Ready){auto media=publishDetailsMedia(value);previewPath=media.thumbnailPath;previewError=media.error;imagePending=false;}
            else if(state==LoaderAsyncTask<DetailsTaskResult>::Failed||state==LoaderAsyncTask<DetailsTaskResult>::Cancelled){imagePending=false;previewError="Picture unavailable";}}
        if(imagePending&&now-imageAt>20000000){imageTask.cancel();imagePending=false;previewError="Picture timed out";}
        #ifdef __vita__
        if(!loaderMenuRedraw(redraw,pad,touches))continue;
#endif
        loaderFrame();drawChrome(origin,settings,"Choose the game whose online information should be used");
        drawTitleMatches(result,selected,pending,hints);
        if(pending){
            const unsigned pulse=unsigned(now/240000)%3;
            for(unsigned i=0;i<3;++i)uiRoundRect(28+float(i)*296,350,288,6,3,i==pulse?Design::focus:Design::line);
        }else if(!result.candidates.empty()){
            uiRoundRect(620,112,328,304,Design::cornerRadius,Design::surface);
            const bool decoded=!previewPath.empty()&&previewTexture.loadAsync(previewPath);
            if(!decoded||!previewTexture.drawFit(previewPath,656,122,256,256,Design::imageRadius))
                uiTextEllipsized(636,242,imagePending||!imageRequested?"Loading picture...":"No picture available",Design::muted,20,296);
            uiTextEllipsized(636,386,previewError.empty()?"Online game preview":previewError,Design::muted,18,296);
        }
        vglSwapBuffers(GL_TRUE);
        // Present loading feedback before scheduling work; coalesce rapid edits
        // and navigation rather than filling either executor with stale jobs.
        if(pending&&!queuedQuery.empty()&&task.settled()){
            const auto entered=queuedQuery;queuedQuery.clear();searchAt=now;
            pending=task.start(scheduleTitleSearchTask,[entered](const std::atomic<bool>& cancelled){return requestTitleSearch(entered,cancelled);});
            if(!pending)result.error="Title search is busy. Edit the title to retry.";
        }
        if(desired&&!imageRequested&&!pending&&imageTask.settled()&&now-imageAt>=180000){
            const auto item=result.candidates[selected];imageRequested=true;imageAt=now;
            const bool imageNetwork=settings.downloadIcons;
            imagePending=imageTask.start(scheduleArtworkTask,[item,imageNetwork](const std::atomic<bool>& cancelled){return requestTitlePreview(item,cancelled,imageNetwork);});
            if(!imagePending)previewError="Picture unavailable";
        }
    }
}
