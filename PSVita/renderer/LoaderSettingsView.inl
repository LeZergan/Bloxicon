// Included by LoaderUI.h inside VitaBrowser after shared UI helpers.
inline void drawSettings(const RuntimeSettings& settings, size_t selected,
                         LoaderInputHints& hints)
{
    selected = std::min(selected, size_t(SettingsUI::FieldCount - 1));
    const size_t active = SettingsUI::sectionIndex(selected);
    const bool signedIn=!playerNameEditable(settings);
    // A quiet rail leaves the selected section obvious without competing cards.
    for (size_t i = SettingsUI::categoryFirst(selected); i < SettingsUI::categoryEnd(selected); ++i)
    {
        const Design::Rect box = SettingsUI::categoryRect(i,selected);
        const bool focused = i == active;
        if (focused) {
            uiRoundRect(box.x, box.y, box.w, box.h, 8, Design::surface);
            uiRoundRect(box.x + 4, box.y + 10, Design::focusWidth + 1, box.h - 20, 2, Design::muted);
        }
        uiText(box.x + 12, box.y + 12, SettingsUI::sectionTitle(i),
            focused ? Design::ink : Design::muted, 20, box.w - 24, focused);
    }
    uiText(Design::detailX, Design::contentTop, SettingsUI::sectionTitle(active), Design::ink, 28, 490, true);
    const auto& category=SettingsUI::activeSection(selected);
    if(category.count>SettingsUI::visibleRows){
        for(bool next:{false,true}){
            const auto box=SettingsUI::pageArrow(next);
            const bool enabled=next?SettingsUI::visibleEnd(selected)<category.first+category.count:SettingsUI::visibleFirst(selected)>category.first;
            uiButtonRect(box,false,enabled);
            uiText(box.x+15,box.y+12,next?">":"<",enabled?Design::focus:Design::muted,20,20,true);
        }
        uiText(822,74,std::to_string(selected-category.first+1)+" / "+std::to_string(category.count),Design::muted,20,76,true);
    }else uiText(856,72,std::to_string(selected-category.first+1)+" / "+std::to_string(category.count),Design::muted,20,92,true);
    const auto& rows = SettingsUI::rows();
    for (size_t i = SettingsUI::visibleFirst(selected); i < SettingsUI::visibleEnd(selected); ++i)
    {
        const Design::Rect box = SettingsUI::rowRect(i,selected);
        const bool focused = i == selected;
        const bool accountName=rows[i].field==SettingsUI::PlayerName&&signedIn;
        uiRoundRect(box.x, box.y, box.w, box.h, 10, focused && !accountName ? Design::focusFill : Design::surface);
        if (focused && !accountName) uiRoundRect(box.x + 4, box.y + 8, 4, box.h - 16, 2, Design::focus);
        const std::string text = SettingsUI::value(settings, rows[i].field);
        const unsigned color=accountName?Design::muted:rows[i].field==SettingsUI::DiagnosticLogging&&settings.performanceLogging?Design::warning:focused?Design::focus:Design::ink;
        const float valueBudget=std::floor(box.w*(rows[i].field==SettingsUI::PlayerName?.50f:.42f));
        const auto measure=[&](float size){return loaderFont().measure(text,size,120,true);};
        const float valueSize=Typography::fitPixels(24,valueBudget,measure);
        const float valueWidth=std::min(valueBudget,std::ceil(measure(valueSize)));
        const float labelWidth=box.w-56-valueWidth;
        uiTextEllipsized(box.x+16,box.y+6,accountName?"Account nickname (locked)":rows[i].label,
            accountName?Design::muted:Design::ink,20,labelWidth,focused);
        const char* detail=accountName?"Sign out to edit your offline name.":
            signedIn && rows[i].field==SettingsUI::AccountConnection?"Signed in to Bloxicon.":rows[i].detail;
        uiTextEllipsized(box.x+16,box.y+30,detail,Design::muted,18,labelWidth);
        uiTextEllipsized(box.x+box.w-16-valueWidth,box.y+(box.h-valueSize)*.5f,text,color,valueSize,valueWidth,true);
    }
    const auto field=rows[selected].field;
    const bool adjusts=field!=SettingsUI::AccountConnection && field!=SettingsUI::PlayerName &&
        field!=SettingsUI::BadgeCollection && field!=SettingsUI::PlaceDataManager &&
        field!=SettingsUI::ContentPacks && field!=SettingsUI::PlaceFixes;
    hints.draw(Design::margin, Design::promptsY, 936, {
        {RBX::SDLK_GAMEPAD_BUTTONA, field==SettingsUI::PlayerName?(signedIn?"":"Edit name"):
            field==SettingsUI::BadgeCollection?"View badges":field==SettingsUI::AccountConnection?"Open account":adjusts?"Change":"Open"},
        {RBX::SDLK_GAMEPAD_DPADLEFT, adjusts?"Adjust":"", RBX::SDLK_GAMEPAD_DPADRIGHT},
        {RBX::SDLK_GAMEPAD_BUTTONX, "Help"},
        {RBX::SDLK_GAMEPAD_BUTTONY, "Reset defaults"},
        {RBX::SDLK_GAMEPAD_BUTTONL1, "Tabs", RBX::SDLK_GAMEPAD_BUTTONR1}}, 14);
}
