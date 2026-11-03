// Included by LoaderUI.h inside VitaBrowser after shared UI helpers.
inline void drawSettings(const RuntimeSettings& settings, size_t selected,
                         LoaderInputHints& hints)
{
    selected = std::min(selected, size_t(SettingsUI::FieldCount - 1));
    const size_t active = SettingsUI::sectionIndex(selected);
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
        const bool accountName=rows[i].field==SettingsUI::PlayerName&&!playerNameEditable(settings);
        uiRoundRect(box.x, box.y, box.w, box.h, 10, focused && !accountName ? Design::focusFill : Design::surface);
        if (focused && !accountName) uiRoundRect(box.x + 4, box.y + 8, 4, box.h - 16, 2, Design::focus);
        uiTextEllipsized(box.x + 16, box.y + 6, accountName?"Account nickname  /  Locked":rows[i].label, accountName?Design::muted:Design::ink, 20, 476, focused);
        uiTextEllipsized(box.x + 16, box.y + 30, accountName?"Sign out to use an offline nickname.":rows[i].detail, Design::muted, 18, 476);
        const std::string text = SettingsUI::value(settings, rows[i].field);
        const unsigned color=accountName?Design::muted:rows[i].field==SettingsUI::DiagnosticLogging&&settings.performanceLogging?Design::warning:focused?Design::focus:Design::ink;
        const float valueSize = loaderFont().measure(text,26,120,true) > 222 ? 18.f : 26.f;
        const float valueWidth=std::min(222.f,loaderFont().measure(text,valueSize,120,true));
        uiTextEllipsized(box.x + box.w - 12 - valueWidth, box.y + (box.h-valueSize)*.5f, text, color, valueSize, valueWidth, true);
    }
    hints.draw(Design::margin, Design::promptsY, 936, {
        {RBX::SDLK_GAMEPAD_BUTTONA, selected == SettingsUI::PlayerName ? (playerNameEditable(settings)?"Edit name":"Locked") : selected == SettingsUI::BadgeCollection ? "View badges" : "Change"},
        {RBX::SDLK_GAMEPAD_DPADLEFT, selected==SettingsUI::PlayerName&&!playerNameEditable(settings)?"":"Adjust", RBX::SDLK_GAMEPAD_DPADRIGHT},
        {RBX::SDLK_GAMEPAD_BUTTONX, "Description"},
        {RBX::SDLK_GAMEPAD_BUTTONY, "Defaults"},
        {RBX::SDLK_GAMEPAD_BUTTONL1, "Tabs", RBX::SDLK_GAMEPAD_BUTTONR1}}, 14);
}
