#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <LWS/Bitmap.hpp>
namespace LWSSettings::internal
{
    LWS::BitmapSharedPtr DefaultSettingsIcon();
    std::expected<LWS::BitmapSharedPtr, Error> LoadSettingsIcon(const std::filesystem::path& path);
}  // namespace LWSSettings::internal
