// Production place details. All metadata comes from Bloxicon's exact original.
inline const char* placeTestLabel(const std::string& status){
    if(status=="verified")return "Gameplay verified";
    if(status=="partial")return "Partial gameplay";
    if(status=="native_loading")return "Loading tested";
    if(status=="broken")return "Reported not working";
    return "Not tested yet";
}
inline const char* placeServerLabel(const DetailsMedia& media){
    if(!hostedSampleFresh(media.serverKnown,media.serverSampledAt))return "Check when joining";
    if(media.serverStatus=="available"||media.serverStatus=="online")return "Ready to join";
    if(media.serverStatus=="playing")return "Game running";
    if(media.serverStatus=="starting")return "Starting";
    if(media.serverStatus=="full")return "Server full";
    if(media.serverStatus=="busy")return "Host busy";
    if(media.serverStatus=="stopping")return "Stopping";
    if(media.serverStatus=="disabled")return "Not hosted";
    if(media.serverStatus=="failed")return "Temporarily unavailable";
    return "Offline";
}
inline void drawGameDetailsContent(const std::string& title,const std::string& creator,const std::string& year,
    const std::string& source,const std::string& state,const DetailsMedia& media,const std::vector<std::string>& description,
    const GameDetails::DisplayInfo& display,unsigned tab,size_t descriptionPage,const std::string& downloadSize,
    const std::string& localSize,const std::string& sharedSize,bool sharedPending,bool sizeWarning)
{
    (void)title;(void)creator;(void)year;(void)display;(void)sharedSize;(void)sharedPending;
    uiRoundRect(328,60,620,316,Design::cornerRadius,Design::surface);
    for(unsigned i=0;i<GameDetails::TabCount;++i){const auto r=GameDetails::tabRect(i);const bool active=i==tab;
        drawBrowseFilter(r,GameDetails::tabName(i),active);
    }
    if(tab==GameDetails::Description){
        const size_t first=descriptionPage*GameDetails::descriptionRows;
        for(size_t i=first;i<std::min(first+GameDetails::descriptionRows,description.size());++i)
            uiText(348,132+float(i-first)*28,description[i],Design::ink,20,580);
        if(description.size()>GameDetails::descriptionRows)
            uiText(856,348,std::to_string(descriptionPage+1)+" / "+std::to_string(GameDetails::pages(description.size())),Design::muted,18,72);
    }else if(tab==GameDetails::Storage){
        const char* labels[]={"Download","Local data","Source","On this Vita"};
        const std::string values[]={downloadSize,localSize,source,state};
        for(unsigned i=0;i<4;++i){const float y=140+float(i)*48;
            uiText(348,y,labels[i],Design::muted,18,156);
            uiTextEllipsized(516,y,values[i],Design::ink,20,412,true);
        }
        if(sizeWarning)uiText(348,340,"Use Join server for this large place.",Design::warning,18,580);
    }else{
        const auto tile=[&](float x,float y,const char* label,const std::string& value,
            const std::string& note,unsigned ink){
            uiRoundRect(x,y,290,102,Design::controlRadius,Design::subtle);
            uiText(x+14,y+12,label,Design::muted,18,262);
            uiTextEllipsized(x+14,y+36,value,ink,23,262,true);
            if(!note.empty())uiTextEllipsized(x+14,y+68,note,Design::muted,17,262);
        };
        const std::string author=media.uploadedBy.empty()?(creator.empty()?"Archive collection":creator):media.uploadedBy;
        tile(340,132,"Source",author,year.empty()?state:year+"  /  "+state,Design::ink);
        tile(642,132,"Compatibility",placeTestLabel(media.testStatus),
            media.testedBy.empty()?"":"Tested by "+media.testedBy,media.testStatus=="verified"?Design::positive:Design::ink);
        tile(340,246,"Online server",placeServerLabel(media),
            media.maxPlayers?std::to_string(media.players)+" / "+std::to_string(media.maxPlayers)+" players":"",Design::ink);
        std::string since;
        if(media.launchSince){
            const std::time_t began=static_cast<std::time_t>(media.launchSince);char date[40]={};
            if(const auto* time=std::gmtime(&began))std::strftime(date,sizeof(date),"Since %d %b %Y",time);
            since=date;
        }else if(media.launchScope=="since_host_start")since="Since this host started";
        tile(642,246,"Server launches",media.launchesKnown?std::to_string(media.launches):"Unavailable",
            media.launchesKnown?since:"",Design::ink);
    }
}
