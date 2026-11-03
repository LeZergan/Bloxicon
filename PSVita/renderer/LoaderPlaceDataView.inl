#include "LoaderPlaceStorageView.inl"
template<class Sampler>
void showPlaceDataManager(Sampler& sampler,const RuntimeSettings& settings,const std::vector<Place>& local) {
    std::vector<Place> entries=PlaceStorage::entries(local);
    std::sort(entries.begin(),entries.end(),[](const Place& a,const Place& b){return detailTitle(a.name)<detailTitle(b.name);});
    size_t selected=0,record=0,textPage=0,mode=0;std::string message;
    PlaceData::Snapshot snapshot;std::vector<std::string> lines;std::string selectedPath;
    LoaderAsyncTask<PlaceData::Snapshot> inspection;LoaderAsyncTask<std::string> backup;
    LoaderAsyncTask<PlaceStorage::Result> deletion;bool deleting=false,confirmValueDelete=false;
    bool pending=false,backupPending=false;size_t placeBytes=0;long long placeId=0;
    auto metadata=[&](){placeBytes=fileSize(entries[selected].path);placeId=artworkPlaceId(entries[selected].path);};
    auto refresh=[&](){if(entries.empty())return;const auto root=placeDataRoot(entries[selected].path);
        inspection.cancel();pending=inspection.start(scheduleCacheSizeTask,[root](const std::atomic<bool>&){return PlaceData::inspect(root);});};
    auto readLines=[&](const std::string& value){lines=detailDescriptionLines(value,90,32768);textPage=0;};
    LoaderInputHints hints;SceCtrlData pad={};SceTouchData rear={},initial={};std::deque<SceTouchData> touches;
    sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);unsigned previous=pad.buttons;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);sceTouchPeek(SCE_TOUCH_PORT_FRONT,&initial,1);
    bool wasTouch=initial.reportNum!=0;Design::StickNavigation menuStick;unsigned previousNav=0;uint64_t nextRepeat=0;
    #ifdef __vita__
    LoaderRenderCadence redraw(100000);
#endif
    for(;;) {
        if(!entries.empty()&&selectedPath!=entries[selected].path){selectedPath=entries[selected].path;snapshot={};record=0;metadata();refresh();}
        if(pending){PlaceData::Snapshot value;std::string error;const auto state=inspection.poll(value,error);
            if(state==LoaderAsyncTask<PlaceData::Snapshot>::Ready){snapshot=std::move(value);record=std::min(record,snapshot.records.empty()?size_t(0):snapshot.records.size()-1);pending=false;}
            else if(state==LoaderAsyncTask<PlaceData::Snapshot>::Failed){snapshot.error=error;pending=false;}}
        if(backupPending){std::string value,error;const auto state=backup.poll(value,error);
            if(state==LoaderAsyncTask<std::string>::Ready){message=value;backupPending=false;}
            else if(state==LoaderAsyncTask<std::string>::Failed){message=error;backupPending=false;}}
        if(deleting){PlaceStorage::Result value;std::string error;const auto state=deletion.poll(value,error);
            if(state==LoaderAsyncTask<PlaceStorage::Result>::Ready){deleting=false;confirmValueDelete=false;message=value.errors.empty()?"Saved value deleted.":"Could not finish deletion: "+value.errors.front();refresh();}
            else if(state==LoaderAsyncTask<PlaceStorage::Result>::Failed){deleting=false;confirmValueDelete=false;message=error;}}
        sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,Design::acceptCircle);applyLoaderStick(pad,menuStick);
        unsigned pressed=pad.buttons&~previous;previous=pad.buttons;
        const uint64_t now=sceKernelGetSystemTimeWide();const unsigned nav=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        if(nav&&(nav!=previousNav||now>=nextRepeat)){pressed|=nav;nextRepeat=now+(nav!=previousNav?330000:105000);}previousNav=nav;
        for(const auto& touch:touches) {
            if(touch.reportNum&&!wasTouch) {
                const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX));
                const float y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
                if(!deleting&&queueLoaderTab(x,y,PageSettings))return;
                if(mode<2)for(size_t i=0;i<4;++i)if(Design::Rect{12,112.f+float(i)*84,mode?936.f:464.f,76}.contains(x,y)) {
                    const size_t candidate=(mode?record:selected)/4*4+i;
                    if(mode&&candidate<snapshot.records.size()){record=candidate;pressed|=SCE_CTRL_CROSS;}
                    if(!mode&&candidate<entries.size())selected=candidate;
                }
                pressed|=loaderControlMask(hints.hit(x,y));
                if(mode==0&&Design::Rect{812,60,136,44}.contains(x,y))pressed|=SCE_CTRL_START;
            }wasTouch=touch.reportNum!=0;
        }
        if(deleting)pressed=0;
        if(pressed&SCE_CTRL_CIRCLE){if(confirmValueDelete){confirmValueDelete=false;message.clear();}else{if(mode==0)return;mode=mode==2?1:0;}}
        const size_t oldRecord=record,oldSelected=selected;
        if(mode<2) {
            size_t& focus=mode?record:selected;const size_t count=mode?snapshot.records.size():entries.size();
            if((pressed&SCE_CTRL_UP)&&focus)--focus;
            if((pressed&SCE_CTRL_DOWN)&&focus+1<count)++focus;
            if((pressed&(SCE_CTRL_LTRIGGER|SCE_CTRL_LEFT))&&focus>=4)focus-=4;
            if((pressed&(SCE_CTRL_RTRIGGER|SCE_CTRL_RIGHT))&&count)focus=std::min(focus+4,count-1);
        }else {
            if((pressed&(SCE_CTRL_UP|SCE_CTRL_LTRIGGER))&&textPage)--textPage;
            if((pressed&(SCE_CTRL_DOWN|SCE_CTRL_RTRIGGER))&&(textPage+1)*9<lines.size())++textPage;
        }
        if(record!=oldRecord||selected!=oldSelected){confirmValueDelete=false;message.clear();}
        if(!entries.empty()&&(pressed&SCE_CTRL_CROSS)) {
            if(mode>=2) { mode=mode==2?1:0; }
            else if(mode==0){mode=1;record=0;}else if(mode==1&&!snapshot.records.empty()) {
                const auto& item=snapshot.records[record];readLines(item.error.empty()?item.value:item.error);mode=2;
            }
        }
        if((pressed&SCE_CTRL_TRIANGLE)&&mode==0&&!entries.empty()&&!backupPending&&!pending) {
            const auto source=placeDataRoot(entries[selected].path);
            const std::string target="ux0:/data/RBXV00001/system/local-saves/backups/"+placeDataKey(entries[selected].path)+"/"+std::to_string(std::time(nullptr));
            backupPending=backup.start(scheduleCacheSizeTask,[source,target](const std::atomic<bool>& cancelled){
                if(cancelled.load())return std::string();PlaceData::backup(source,target);return std::string("Backup verified. Original saves kept.");});
        }
        if((pressed&SCE_CTRL_SQUARE)&&!entries.empty()) {
            std::string text;const auto root=placeDataRoot(entries[selected].path);
            for(const char* file:{"place.info","load-report.txt","missing-assets.txt"})try{text+=std::string(file)+"\n"+PlaceData::raw(root+"/"+file)+"\n";}catch(...){}
            readLines(text.empty()?"No load report has been recorded for this game yet.":text);mode=3;
        }
        if((pressed&SCE_CTRL_START)&&mode==0&&!entries.empty()&&!pending&&!backupPending) {
            if(showPlaceStorageActions(sampler,settings,entries[selected])) {
                entries.erase(entries.begin()+selected);selected=std::min(selected,entries.empty()?size_t(0):entries.size()-1);
            }
            selectedPath.clear();snapshot={};mode=0;
            sampler.read(pad,touches,rear);previous=pad.buttons;wasTouch=true;
        }
        if((pressed&SCE_CTRL_START)&&mode==1&&!entries.empty()&&!snapshot.records.empty()&&!pending&&!backupPending) {
            if(!confirmValueDelete){confirmValueDelete=true;message="Delete this saved value? Press Start again. Circle cancels.";}
            else {
                const auto root=placeDataRoot(entries[selected].path);const auto relative=snapshot.records[record].file.relative;
                if(PlaceData::safeRelative(relative))deleting=deletion.start(scheduleCacheSizeTask,[root,relative](const std::atomic<bool>&){
                    std::vector<PlaceStorage::Target> files;const std::string path=root+"/"+relative;
                    for(const char* suffix:{"",".bak",".tmp",".previous"})files.push_back({path+suffix,root});
                    return PlaceStorage::remove(files);});
                if(!deleting){confirmValueDelete=false;message="Storage is busy. Try again.";}
            }
        }
        #ifdef __vita__
        if(!loaderMenuRedraw(redraw,pad,touches))continue;
#endif
        loaderFrame();drawChrome(PageSettings,settings,deleting?"Place data / Deleting saved value":backupPending?"Place data / Creating verified backup":
            pending?"Place data / Reading saves":!message.empty()?message:"Place data / "+settings.username);
        uiTextEllipsized(12,60,entries.empty()?"Place data":detailTitle(entries[selected].name),kUiText,28,760,true);
        if(mode==0&&!entries.empty()) {uiRoundRect(812,60,136,44,8,kUiPanel);uiText(824,73,"Actions >",kUiText,18,110,true);}
        if(mode==0) {
            uiText(12,94,"Storage manager  >  Choose game  (1 / 2)",Design::muted,18,740);
            const size_t base=selected/4*4;
            for(size_t i=base;i<std::min(base+4,entries.size());++i) {
                const float y=112+float(i-base)*84;
                uiRoundRect(12,y,464,76,10,i==selected?kUiPanelSelected:kUiPanel);
                if(i==selected)uiFocusFrame({12,y,464,76});
                uiTextEllipsized(24,y+26,detailTitle(entries[i].name),kUiText,24,440,i==selected);
            }
            uiRoundRect(484,112,464,328,10,kUiPanel);
            uiText(496,124,"SAVED DATA",kUiMuted,18,440,true);
            uiText(496,152,std::to_string(snapshot.records.size())+" entries  /  "+(snapshot.bytes?detailSizeText(snapshot.bytes):"0 bytes"),kUiText,22,440,true);
            uiText(496,188,snapshot.modified?"Updated  "+badgeDate(snapshot.modified):"No saved data yet",kUiMuted,18,440);
            if(!entries.empty()) {
                uiText(496,224,"Place file  "+detailSizeText(placeBytes),kUiMuted,18,440);
                uiText(496,252,"Bloxicon place ID  "+(placeId?std::to_string(placeId):"Unknown"),kUiMuted,18,440);
                uiText(496,292,"Start opens Game actions (step 2).",kUiMuted,16,440);
                uiText(496,316,"Delete the place, saves, pictures or reports.",kUiMuted,16,440);
                if(!snapshot.error.empty())uiTextEllipsized(496,348,snapshot.error,kUiRed,16,440);
            }
            if(entries.empty())uiText(48,164,"No places installed yet.",kUiMuted,22,840);
        }else if(mode==1) {
            const size_t base=record/4*4;
            for(size_t i=base;i<std::min(base+4,snapshot.records.size());++i) {
                const auto& item=snapshot.records[i];const float y=112+float(i-base)*84;
                uiRoundRect(12,y,936,76,10,i==record?kUiPanelSelected:kUiPanel);if(i==record)uiFocusFrame({12,y,936,76});
                uiTextEllipsized(24,y+14,item.key,item.error.empty()?kUiText:kUiRed,24,650,i==record);
                uiTextEllipsized(24,y+44,item.type+" / "+item.store+" / "+item.scope,kUiMuted,18,750);
                uiText(806,y+21,detailSizeText(item.file.bytes),kUiMuted,16,112);
            }
            if(snapshot.records.empty())uiText(48,166,pending?"Reading saved data...":"This game has not written any saved values yet.",kUiMuted,22,840);
        }else {
            uiRoundRect(12,112,936,328,10,kUiPanel);
            if(mode==2&&!snapshot.records.empty())uiTextEllipsized(42,140,snapshot.records[record].store+" / "+snapshot.records[record].scope+" / "+snapshot.records[record].key,kUiBlue,18,876,true);
            const size_t base=textPage*9;for(size_t i=base;i<std::min(base+9,lines.size());++i)
                uiText(42,160+float(i-base)*25,lines[i],kUiText,18,876);
            uiText(806,98,std::to_string(textPage+1)+" / "+std::to_string(std::max(size_t(1),(lines.size()+8)/9)),kUiMuted,16,130);
        }
        if(mode==0)hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Saved values"},{RBX::SDLK_GAMEPAD_BUTTONY,"Backup"},
            {RBX::SDLK_GAMEPAD_BUTTONX,"Load report"},{RBX::SDLK_GAMEPAD_BUTTONSTART,"Game actions"},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}},14);
        else if(mode==1)hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"View value"},
            {RBX::SDLK_GAMEPAD_BUTTONSTART,confirmValueDelete?"Confirm delete":"Delete value"},{RBX::SDLK_GAMEPAD_BUTTONL2,"Pages",RBX::SDLK_GAMEPAD_BUTTONR2},{RBX::SDLK_GAMEPAD_BUTTONB,confirmValueDelete?"Cancel":"Back"}},14);
        else hints.draw(Design::margin,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,"Done"},
            {RBX::SDLK_GAMEPAD_BUTTONL2,"Pages",RBX::SDLK_GAMEPAD_BUTTONR2},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}},14);
        vglSwapBuffers(GL_FALSE);sceKernelDelayThread(8000);
    }
}
