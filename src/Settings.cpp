#include "pch.h"
#include "Settings.h"

#include <filesystem>

Settings& Settings::GetSingleton()
{
    static Settings singleton;
    return singleton;
}

std::string Settings::GetConfigPath() const
{
    return "Data/SKSE/Plugins/Horde/settings.json";
}

void Settings::Load()
{
    auto path = GetConfigPath();

    std::ifstream file(path);
    if (!file.is_open()) {
        logger::info("Config file not found, creating defaults: {}", path);
        Save();
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        const int loadedVersion = j.value("settingsVersion", 1);
        _settingsVersion = loadedVersion;

        _modifierKey          = j.value("modifierKey", _modifierKey);
        _activateKey          = j.value("activateKey", _activateKey);
        _groupModifierKey     = j.value("groupModifierKey", _groupModifierKey);
        _followAllKey         = j.value("followAllKey", _followAllKey);
        _waitAllKey           = j.value("waitAllKey", _waitAllKey);
        _summonAllKey         = j.value("summonAllKey", _summonAllKey);
        _passiveAllKey        = j.value("passiveAllKey", _passiveAllKey);
        _maxFollowers         = j.value("maxFollowers", _maxFollowers);
        // Hand-edited configs must also respect the available alias slots.
        _maxFollowers         = std::clamp(_maxFollowers, 1, kMaxFollowerCeiling);
        _defaultSandboxEnabled = j.value("defaultSandboxEnabled", _defaultSandboxEnabled);
        _defaultFollowClose   = j.value("defaultFollowClose", _defaultFollowClose);
        _useKeybinds          = j.value("useKeybinds", _useKeybinds);
        _notificationsEnabled = j.value("notificationsEnabled", _notificationsEnabled);
        _followDistance       = j.value("followDistance", _followDistance);
        const auto glyphs = j.value("controllerGlyphs", std::string("xbox"));
        _controllerGlyphs = glyphs == "playstation" || glyphs == "generic" ? glyphs : "xbox";

        logger::info("Settings loaded from {}", path);

        // Migrate the v1 default cap from 10 to 20 once, preserving later choices.
        if (loadedVersion < 2) {
            if (!j.contains("maxFollowers") || _maxFollowers == 10) {
                logger::info("Settings: Migrating maxFollowers from {} to {}", _maxFollowers, kMaxFollowerCeiling);
                _maxFollowers = kMaxFollowerCeiling;
            }
        }

        // Expose new settings keys when upgrading older configs.
        if (loadedVersion < kSettingsVersion) {
            _settingsVersion = kSettingsVersion;
            Save();
        }
    } catch (const nlohmann::json::exception& e) {
        logger::error("Failed to parse config: {} — using defaults", e.what());
    }
}

void Settings::Save() const
{
    auto path = GetConfigPath();

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    nlohmann::json j;
    j["settingsVersion"]       = _settingsVersion;
    j["modifierKey"]          = _modifierKey;
    j["activateKey"]          = _activateKey;
    j["groupModifierKey"]     = _groupModifierKey;
    j["followAllKey"]         = _followAllKey;
    j["waitAllKey"]           = _waitAllKey;
    j["summonAllKey"]         = _summonAllKey;
    j["passiveAllKey"]        = _passiveAllKey;
    j["maxFollowers"]         = _maxFollowers;
    j["defaultSandboxEnabled"] = _defaultSandboxEnabled;
    j["defaultFollowClose"]    = _defaultFollowClose;
    j["useKeybinds"]           = _useKeybinds;
    j["notificationsEnabled"]  = _notificationsEnabled;
    j["followDistance"]         = _followDistance;
    j["controllerGlyphs"]       = _controllerGlyphs;

    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(4);
        logger::info("Settings saved to {}", path);
    } else {
        logger::error("Failed to save settings to {}", path);
    }
}

void Settings::SetMaxFollowers(int v)
{
    _maxFollowers = std::clamp(v, 1, kMaxFollowerCeiling);
    Save();
}

void Settings::SetDefaultSandboxEnabled(bool v)
{
    _defaultSandboxEnabled = v;
    Save();
}

void Settings::SetDefaultFollowClose(bool v)
{
    _defaultFollowClose = v;
    Save();
}

void Settings::SetUseKeybinds(bool v)
{
    _useKeybinds = v;
    Save();
}

void Settings::SetNotificationsEnabled(bool v)
{
    _notificationsEnabled = v;
    Save();
}

void Settings::SetFollowDistance(const std::string& preset)
{
    if (preset == "close" || preset == "normal" || preset == "far") {
        _followDistance = preset;
        Save();
    }
}

void Settings::Notify(const char* msg)
{
    if (GetSingleton().GetNotificationsEnabled()) {
        RE::SendHUDMessage::ShowHUDMessage(msg);
    }
}
