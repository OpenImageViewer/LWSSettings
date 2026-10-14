#pragma once
#include <LWSUI/Theme.hpp>
namespace LWSSettings
{
    // Each section is a reusable typed style. Geometry is logical pixels; fonts do
    // not scale unrelated dimensions. JSON decoding lives solely in ThemeStorage.
    struct SettingsLayout
    {
        float horizontalPadding = 160.f / 7, sectionGap = 64.f / 7;
        float headerHeight = 480.f / 7, searchHeight = 256.f / 7;
        float descriptionHeight = 1168.f / 7, footerHeight = 352.f / 7;
        float statusHeight = 224.f / 7, actionWidth = 640.f / 7, actionGap = 40.f / 7;
        float descriptionTitleGap = 32.f / 7;
        float descriptionMaxFraction = .75f;
    };
    struct SettingsTheme
    {
        LWSUI::Theme base;
        LWSUI::Theme header, search, tree, description, footer, controls, popups;
        SettingsLayout layout;
        SettingsTheme() : SettingsTheme(LWSUI::ThemePreset::Light) {}
        explicit SettingsTheme(LWSUI::ThemePreset preset)
        {
            base = LWSUI::MakeTheme(preset);
            base.borderWidth = 1.0f;
            base.checkerSize = 8.0f;
            base.editorFraction = .33f;
            base.editorMinLeft = 210.57142639160156f;
            base.focusWidth = 1.0f;
            base.menuMaxRows = 6.0f;
            base.menuRowHeight = 27.0f;
            base.padding = 8.0f;
            base.resetWidth = 544.f / 7;
            base.rowHeight = 29.28571319580078f;
            base.swatchGap = 4.0f;
            base.swatchWidth = 40.0f;
            base.textPadding = 6.0f;
            base.treeRowHeight = 22.0f;
            base.treeRowPadding = 1.0f;
            base.font.italic = false;
            base.font.size = 13.0f;
            base.glyphFont.size = 10.0f;
            base.headingFont.size = 16.0f;
            base.smallFont.size = 12.0f;
            header = search = tree = description = footer = controls = popups = base;
            search.spacing = 8; // Separate the search field from the value-filter selector.
            footer.background = base.surface;
            popups.background = base.popupSurface;
            popups.titleFont.size = 136.f / 7;
        }
    };
}  // namespace LWSSettings
