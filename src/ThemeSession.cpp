#include <LWSSettings/ThemeStorage.hpp>
#include "ThemeFields.hpp"
#include <LWSSettings/ThemeSession.hpp>
namespace LWSSettings
{
    Status ThemeSession::Load(const std::filesystem::path& defaults)
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(file_, error);
        if (error) return std::unexpected(Error{file_.string(), error.message()});
        const auto source = exists ? file_ : defaults;
        auto profile = source.empty() ? std::expected<ThemeProfile, Error>{ThemeProfile{}}
                                      : ThemeStorage::LoadProfileFile(source);
        if (!profile) return std::unexpected(profile.error());
        auto resolved = ThemeFields::Resolve(profile->overrides, profile->preset);
        if (!resolved) return std::unexpected(resolved.error());
        current_ = saved_ = std::move(*profile); theme_ = std::move(*resolved);
        OnChange.Raise(); return {};
    }
    Status ThemeSession::SetProfile(ThemeProfile profile)
    {
        if (profile == current_) return {};
        auto resolved = ThemeFields::Resolve(profile.overrides, profile.preset);
        if (!resolved) return std::unexpected(resolved.error());
        current_ = std::move(profile); theme_ = std::move(*resolved);
        OnChange.Raise(); return {};
    }
    Status ThemeSession::Save()
    {
        auto result = ThemeStorage::SaveProfileFile(file_, current_);
        if (result) { saved_ = current_; OnChange.Raise(); }
        return result;
    }
    void ThemeSession::Revert() { (void) SetProfile(saved_); }
}
