// Friends, requests and news share one spacious layout and native focus model.
inline void drawCommunityButton(const CommunityUI::Button& button,bool selected,bool enabled,const char* replacement=nullptr){
    const auto& r=button.box;uiButtonRect(r,selected,enabled);
    if(selected)uiFocusFrame(r,enabled?Design::focus:Design::muted,Design::controlRadius);
    uiTextEllipsized(r.x+12,r.y+(r.h-20)*.5f,replacement?replacement:button.label,enabled?(selected?Design::focus:Design::ink):Design::muted,20,r.w-24,true);
}
inline void communityHeading(const RuntimeSettings& settings,const std::string& title){
    loaderFrame();drawChrome(PageHome,settings,"",title);
}
inline void drawPersonIcon(PortraitArtwork* portraits,const std::string& name,float x,float y,float size){
    uiRoundRect(x,y,size,size,Design::imageRadius,Design::subtle);
    if(portraits&&portraits->draw(name,x,y,size))return;
    const auto letter=name.substr(0,1);
    const float width=loaderFont().measure(letter,24,1,true);
    uiText(x+(size-width)*.5f,y+(size-24)*.5f,letter,Design::focus,24,width,true);
}
inline void drawCommunityScreen(const RuntimeSettings& settings,LoaderInputHints& hints,const VitaCommunity::Client& client,
    const std::vector<std::string>& lines,size_t textPage,bool confirmBlock,bool confirmRemove=false,unsigned focus=CommunityUI::Entries,size_t actionIndex=0,bool allowDiscovery=true,PortraitArtwork* portraits=nullptr){
    using namespace CommunityUI;using namespace VitaCommunity;
    const unsigned active=client.tab==Conversation?Friends:client.tab;
    communityHeading(settings,client.tab==Conversation?"Chat":tabName(client.tab));
    if(portraits)portraits->beginFrame();
    for(unsigned i=0;i<sectionCount;++i){const auto r=section(i);const auto value=sectionTab(i);const bool selected=value==active;
        if(selected){uiRoundRect(r.x,r.y,r.w,r.h,Design::controlRadius,Design::focusFill);uiRect(r.x+8,r.y+12,3,r.h-24,Design::focus);}
        if(selected&&focus==Sections)uiFocusFrame(r,Design::focus,Design::controlRadius);
        uiText(r.x+20,r.y+16,tabName(value),selected?Design::ink:Design::muted,20,r.w-32,selected);
    }
    const std::string heading=client.tab==Conversation?client.peer:client.tab==Find?(client.query.empty()?"Find a player":client.query):client.tab==News?"Latest news":
        std::to_string(client.page.rows.size())+(client.tab==Friends?(client.page.rows.size()==1?" friend":" friends"):
            client.tab==Requests?(client.page.rows.size()==1?" request":" requests"):(client.page.rows.size()==1?" blocked player":" blocked players"));
    uiTextEllipsized(196,72,heading,Design::ink,26,500,true);
    drawCommunityButton({None,client.tab==Find?"Search":client.tab==Conversation?"Older":"Refresh",toolbar(0)},false,!client.busy());
    if(client.tab==Conversation)drawCommunityButton({None,"Latest",toolbar(1)},false,!client.busy());
    if(client.tab==Find&&client.page.more)drawCommunityButton({None,"Next",toolbar(1)},false,!client.busy());
    if(client.tab==Conversation){
        const auto bubbles=conversationBubbles(client.page,client.selected,textPage,[](const std::string& s){return loaderFont().measure(s,20);},&lines);
        if(bubbles.empty()){uiRoundRect(204,120,744,256,Design::cornerRadius,Design::surface);uiText(228,184,client.busy()?"Loading conversation":"Say hello",Design::ink,28,680,true);}
        for(const auto& bubble:bubbles){const auto& r=bubble.box;const auto& row=client.page.rows[bubble.row];
            uiRoundRect(r.x,r.y,r.w,r.h,Design::cornerRadius,row.mine?Design::focusFill:Design::surface);
            if(focus==Entries&&bubble.row==client.selected)uiFocusFrame(r,Design::focus,Design::cornerRadius);
            uiTextEllipsized(r.x+14,r.y+8,row.title,Design::muted,18,r.w-28,true);
            for(size_t i=0;i<bubble.lines.size();++i)uiText(r.x+14,r.y+34+float(i)*22,bubble.lines[i],Design::ink,20,r.w-28);
        }
    }else if(client.page.rows.empty()){
        uiRoundRect(196,120,752,320,Design::cornerRadius,Design::surface);
        const char* title=client.busy()?"Loading":client.tab==News?"No news yet":client.tab==Requests?"No pending requests":client.tab==Blocked?"No blocked players":client.tab==Find?"Search for a username":"Your friends will appear here";
        uiText(224,160,title,Design::ink,28,696,true);
        if(!client.busy())uiText(224,208,client.tab==Friends?"Find someone to start a conversation.":client.tab==News?"Published posts appear here.":client.tab==Find?"Enter the first three letters or more.":"You're all caught up.",Design::muted,20,696);
    }else{
        const auto count=visibleRows(client.tab),base=client.selected/count*count;
        for(size_t i=base;i<std::min(base+count,client.page.rows.size());++i){const auto r=entry(i-base);const auto& row=client.page.rows[i];const bool selected=i==client.selected;
            uiRoundRect(r.x,r.y,r.w,r.h,Design::controlRadius,selected?Design::focusFill:Design::surface);
            if(selected&&focus==Entries)uiFocusFrame(r,Design::focus,Design::controlRadius);
            if(client.tab==News){const auto titles=Geometry::paragraphLines(row.title,r.w-24,2,[](const std::string& s){return loaderFont().measure(s,20,120,true);});for(size_t n=0;n<titles.size();++n)uiText(r.x+12,r.y+8+float(n)*24,titles[n],Design::ink,20,r.w-24,selected);}
            else{
                drawPersonIcon(client.tab==Blocked?nullptr:portraits,row.title,r.x+10,r.y+8,44);
                uiTextEllipsized(r.x+66,r.y+8,row.title,Design::ink,22,r.w-78,true);
                uiTextEllipsized(r.x+66,r.y+34,row.subtitle,Design::muted,18,r.w-78);
            }
        }
        if(client.page.rows.size()>count){const float height=320.f*float(std::min(count,client.page.rows.size()-base))/float(client.page.rows.size());uiRoundRect(540,120+float(base)*320.f/float(client.page.rows.size()),3,height,1.5f,Design::muted);}
        uiRoundRect(556,120,392,320,Design::cornerRadius,Design::surface);
        const auto& row=client.page.rows[client.selected];
        if(client.tab==News){
            const auto titles=Geometry::paragraphLines(row.title,360,3,[](const std::string& s){return loaderFont().measure(s,24,120,true);});
            for(size_t i=0;i<titles.size();++i)uiText(572,136+float(i)*28,titles[i],Design::ink,24,360,true);
            const auto excerpt=Geometry::paragraphLines(row.body,360,4,[](const std::string& s){return loaderFont().measure(s,20);});
            for(size_t i=0;i<excerpt.size();++i)uiText(572,240+float(i)*26,excerpt[i],Design::muted,20,360);
        }else{
            drawPersonIcon(client.tab==Blocked?nullptr:portraits,row.title,572,136,72);
            uiTextEllipsized(658,145,row.title,Design::ink,26,274,true);
            uiTextEllipsized(658,178,row.subtitle,Design::muted,18,274);
            uiRect(572,316,360,1,Design::line);
        }
    }
    const auto actions=buttons(client,allowDiscovery,lines.size());
    for(size_t i=0;i<actions.size();++i){
        const char* replacement=(confirmBlock&&actions[i].action==Block)?"Confirm block":(confirmRemove&&actions[i].action==Remove)?"Confirm remove":nullptr;
        drawCommunityButton(actions[i],focus==Actions&&i==actionIndex,enabled(actions[i],client,textPage,lines.size()),replacement);
    }
    if(!client.notice.empty()&&client.notice!="Updated / PS Vita"&&client.notice!="Nothing here yet."&&client.notice!="Loading..."&&client.notice!="Loading community...")
        uiTextEllipsized(196,101,client.notice,Design::warning,18,752);
    hints.draw(12,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_BUTTONA,client.tab==News?"Read":"Select"},
        {RBX::SDLK_GAMEPAD_BUTTONX,client.tab==Conversation?"Write":client.tab==News?"":"Actions"},
        {RBX::SDLK_GAMEPAD_BUTTONB,"Back"},{RBX::SDLK_GAMEPAD_BUTTONL1,"Sections",RBX::SDLK_GAMEPAD_BUTTONR1}});
}
inline void drawNewsArticle(const RuntimeSettings& settings,LoaderInputHints& hints,const std::vector<std::string>& lines,size_t first){
    communityHeading(settings,"News");uiRoundRect(12,60,936,380,Design::cornerRadius,Design::surface);
    for(size_t i=first;i<std::min(first+10,lines.size());++i)uiText(40,80+float(i-first)*34,lines[i],Design::ink,22,872,i<2);
    if(lines.size()>10){const float height=3400.f/float(lines.size());uiRoundRect(932,80+float(first)*340.f/float(lines.size()),4,height,2,Design::muted);}
    hints.draw(12,Design::promptsY,936,{{RBX::SDLK_GAMEPAD_DPADUP,"Scroll",RBX::SDLK_GAMEPAD_DPADDOWN},{RBX::SDLK_GAMEPAD_BUTTONB,"Back"}});
}
template<class Sampler>void showNewsArticle(Sampler& sampler,const RuntimeSettings& settings,const VitaCommunity::Row& row){
    // Reserve enough width for the opening lines, which use the bold face.
    auto lines=Geometry::paragraphLines(row.title+"\n\n"+row.body,872,4096,[](const std::string& s){return loaderFont().measure(s,22,120,true);});
    size_t first=0;unsigned previous=~0u;uint64_t repeatAt=0;LoaderInputHints hints;LoaderInputBoundary boundary;boundary.waitForRelease();Design::StickNavigation stick;bool touching=false;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);LoaderRenderCadence redraw(100000);
    for(;;){SceCtrlData pad={};SceTouchData rear={};std::deque<SceTouchData> touches;sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,settings.acceptCircle);applyLoaderStick(pad,stick);
        unsigned pressed=pad.buttons&~previous;previous=pad.buttons;const auto now=sceKernelGetSystemTimeWide();
        for(const auto& touch:touches){if(touch.reportNum&&!touching&&!boundary.blocked()){
            const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX)),y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
            if(queueLoaderTab(x,y,PageHome))return;
            pressed|=loaderControlMask(hints.hit(x,y));if(x>912&&y>=60&&y<440)pressed|=y<250?SCE_CTRL_UP:SCE_CTRL_DOWN;
        }touching=touch.reportNum!=0;}
        if(boundary.suppress(pad.buttons,touching))pressed=0;
        if(!boundary.blocked()&&(pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN))&&now>=repeatAt){pressed|=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN);repeatAt=now+160000;}
        if(pressed&SCE_CTRL_CIRCLE)return;if((pressed&SCE_CTRL_UP)&&first)--first;if((pressed&SCE_CTRL_DOWN)&&first+10<lines.size())++first;
        if(!loaderMenuRedraw(redraw,pad,touches))continue;
        drawNewsArticle(settings,hints,lines,first);vglSwapBuffers(GL_FALSE);sceKernelDelayThread(8000);
    }
}

template<class Sampler>std::string showBloxiconCommunity(Sampler& sampler,const RuntimeSettings& settings,bool allowDiscovery,bool findPlayers,const std::string& initialPlayer){
    using namespace CommunityUI;using namespace VitaCommunity;
    Client client;client.query=initialPlayer;client.switchTab(findPlayers?Find:Friends);
    LoaderInputHints hints;PortraitArtwork portraits;LoaderInputBoundary boundary;boundary.waitForRelease();Design::StickNavigation stick;
    unsigned previous=~0u,focus=Entries;size_t actionIndex=0,textPage=0;uint64_t repeatAt=0,armedUntil=0;Action armed=None;std::string armedTarget;
    SceTouchPanelInfo panel={};sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&panel);bool touching=false;
    LoaderRenderCadence redraw(100000);
    for(;;){if(queuedLoaderPage()!=PageCount)return {};VitaAccount::connector().tick();client.tick();SceCtrlData pad={};SceTouchData rear={};std::deque<SceTouchData> touches;sampler.read(pad,touches,rear);pad.buttons=VitaInput::menuButtons(pad.buttons,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,settings.acceptCircle);applyLoaderStick(pad,stick);
        unsigned pressed=pad.buttons&~previous;previous=pad.buttons;Action action=None;const auto now=sceKernelGetSystemTimeWide();
        const auto body=client.selected<client.page.rows.size()?client.page.rows[client.selected].body:std::string();
        auto lines=Geometry::paragraphLines(body,chatTextWidth,4096,[](const std::string& s){return loaderFont().measure(s,20);});
        auto actions=buttons(client,allowDiscovery,lines.size());actionIndex=std::min(actionIndex,actions.empty()?size_t(0):actions.size()-1);
        if(armed!=None&&now>=armedUntil)armed=None;
        for(const auto& touch:touches){if(touch.reportNum&&!touching&&!boundary.blocked()){
            const float x=(touch.report[0].x-panel.minAaX)*960.f/std::max(1,int(panel.maxAaX-panel.minAaX)),y=(touch.report[0].y-panel.minAaY)*544.f/std::max(1,int(panel.maxAaY-panel.minAaY));
            if(queueLoaderTab(x,y,PageHome))return {};
            bool used=false;
            for(unsigned i=0;i<sectionCount;++i)if(section(i).contains(x,y)){client.switchTab(sectionTab(i));focus=Entries;textPage=0;armed=None;used=true;break;}
            if(!used)for(size_t i=0;i<actions.size();++i)if(actions[i].box.contains(x,y)){focus=Actions;actionIndex=i;action=actions[i].action;used=true;break;}
            if(!used&&toolbar(0).contains(x,y)){action=client.tab==Find?Search:client.tab==Conversation?Older:Refresh;used=true;}
            if(!used&&toolbar(1).contains(x,y)){if(client.tab==Conversation)action=Latest;else if(client.tab==Find)action=NextResults;used=true;}
            if(!used&&client.tab!=Conversation){const auto count=visibleRows(client.tab),base=client.selected/count*count;for(size_t i=0;i<count;++i)if(entry(i).contains(x,y)&&base+i<client.page.rows.size()){const bool same=client.selected==base+i;client.selected=base+i;focus=Entries;textPage=0;if(same)pressed|=SCE_CTRL_CROSS;break;}}
            pressed|=loaderControlMask(hints.hit(x,y));
        }touching=touch.reportNum!=0;}
        if(boundary.suppress(pad.buttons,touching)){pressed=0;action=None;}
        const unsigned nav=pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
        if(!boundary.blocked()&&nav&&now>=repeatAt){pressed|=nav;repeatAt=now+180000;}
        if(pressed&SCE_CTRL_CIRCLE){if(focus==Actions){focus=Entries;armed=None;}else if(client.tab==Conversation){client.closeConversation();focus=Entries;}else return {};}
        if(pressed&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){client.switchTab(nextSection(client.tab,(pressed&SCE_CTRL_LTRIGGER)?-1:1));focus=Entries;textPage=0;armed=None;}
        if(pressed&SCE_CTRL_TRIANGLE)action=Refresh;
        if(pressed&SCE_CTRL_SQUARE){if(client.tab==Conversation)action=Write;else if(client.tab!=News)focus=focus==Actions?Entries:Actions;}
        if(focus==Sections){if(pressed&SCE_CTRL_UP)client.switchTab(nextSection(client.tab,-1));if(pressed&SCE_CTRL_DOWN)client.switchTab(nextSection(client.tab,1));if(pressed&(SCE_CTRL_RIGHT|SCE_CTRL_CROSS))focus=Entries;}
        else if(focus==Entries){
            if(pressed&SCE_CTRL_LEFT)focus=Sections;
            if((pressed&SCE_CTRL_RIGHT)&&!actions.empty())focus=Actions;
            if((pressed&SCE_CTRL_UP)&&client.selected){--client.selected;textPage=0;}
            if((pressed&SCE_CTRL_DOWN)&&client.selected+1<client.page.rows.size()){++client.selected;textPage=0;}
            if(pressed&SCE_CTRL_CROSS){if(client.tab==News)action=ReadArticle;else if(client.tab==Find&&client.page.rows.empty())action=Search;else if(!actions.empty()){focus=Actions;action=actions[0].action;}}
        }else{
            if(pressed&SCE_CTRL_LEFT){if(actionIndex%2)--actionIndex;else focus=Entries;}
            if((pressed&SCE_CTRL_RIGHT)&&actionIndex%2==0&&actionIndex+1<actions.size())++actionIndex;
            if((pressed&SCE_CTRL_UP)&&actionIndex>=2)actionIndex-=2;
            if((pressed&SCE_CTRL_DOWN)&&actionIndex+2<actions.size())actionIndex+=2;
            if((pressed&SCE_CTRL_DOWN)&&actionIndex==1&&actions.size()==3)actionIndex=2;
            if((pressed&SCE_CTRL_CROSS)&&!actions.empty())action=actions[actionIndex].action;
        }
        if(action!=None&&(!client.busy()||(action==Write&&!client.mutating())))try{
            const auto row=client.selected<client.page.rows.size()?client.page.rows[client.selected]:Row();loaderUiSounds().play(UiSound::Press);
            if(action!=Remove&&action!=Block)armed=None;
            switch(action){
            case Refresh:client.retry();break;
            case ReadArticle:if(!row.id.empty()){showNewsArticle(sampler,settings,row);boundary.waitForRelease();previous=~0u;}break;
            case Chat:if(!row.title.empty())client.openConversation(row.title);focus=Entries;break;
            case Accept:case Cancel:case Unblock:case Add:if(!row.title.empty())client.relationship(row,action==Accept?"accept":action==Cancel?"remove":action==Unblock?"unblock":"request");break;
            case Remove:case Block:if(!row.title.empty()){if(armed==action&&armedTarget==row.id&&now<armedUntil){client.relationship(row,action==Remove?"remove":"block");armed=None;}else{armed=action;armedTarget=row.id;armedUntil=now+5000000;}}break;
            case Search:{if(client.tab!=Find)client.switchTab(Find);bool accepted=false;auto query=promptText(u"Find player",client.query,20,&accepted);boundary.waitForRelease();if(accepted){client.stop();client.query=query;client.pageNumber=1;client.selected=0;client.page={};client.refresh();}focus=Entries;break;}
            case Write:{client.stop();bool accepted=false;auto message=promptText(u"Private message",client.draft,400,&accepted);boundary.waitForRelease();if(accepted&&message.find_first_not_of(" \t\r\n")!=std::string::npos)client.message(message);else client.refresh();break;}
            case Report:if(!row.title.empty()){bool accepted=false;auto details=promptText(u"Player report", "",400,&accepted);boundary.waitForRelease();if(accepted&&!details.empty()){VitaAccount::Doc d;d.SetObject();VitaAccount::addString(d,"username",row.title);VitaAccount::addString(d,"reason","other");VitaAccount::addString(d,"details",details);client.start({"/api/v1/reports",VitaAccount::json(d),VitaAccount::randomKey(),true});}}break;
            case Older:if(client.page.more){client.before=client.page.before;client.selected=0;client.refresh();}break;
            case Latest:client.before.clear();client.page={};client.selected=0;client.refresh();break;
            case NextResults:if(client.page.more){++client.pageNumber;client.selected=0;client.refresh();}break;
            case PreviousText:if(textPage)--textPage;break;
            case NextText:if((textPage+1)*textRows<lines.size())++textPage;break;
            default:break;
            }
        }catch(const std::exception& error){client.notice=error.what();client.changed=true;}
        if(!loaderMenuRedraw(redraw,pad,touches)&&!client.changed)continue;
        client.changed=false;drawCommunityScreen(settings,hints,client,lines,textPage,armed==Block,armed==Remove,focus,actionIndex,allowDiscovery,&portraits);vglSwapBuffers(GL_FALSE);sceKernelDelayThread(8000);
    }
}
