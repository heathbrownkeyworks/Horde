#pragma once
#include "pch.h"

class Settings
{
public:
    static Settings& GetSingleton();

    void Load();
    void Save() const;

    uint32_t GetModifierKey() const { return _modifierKey; }
    uint32_t GetActivateKey() const { return _activateKey; }
    uint32_t GetGroupModifierKey() const { return _groupModifierKey; }
    uint32_t GetFollowAllKey() const { return _followAllKey; }
    uint32_t GetWaitAllKey() const { return _waitAllKey; }
    uint32_t GetSummonAllKey() const { return _summonAllKey; }
    uint32_t GetPassiveAllKey() const { return _passiveAllKey; }
    int      GetMaxFollowers() const { return _maxFollowers; }
    bool     GetDefaultSandboxEnabled() const { return _defaultSandboxEnabled; }
    bool     GetDefaultFollowClose() const { return _defaultFollowClose; }
    bool     GetUseKeybinds() const { return _useKeybinds; }
    bool     GetNotificationsEnabled() const { return _notificationsEnabled; }
    const std::string& GetFollowDistance() const { return _followDistance; }

    void SetMaxFollowers(int v);
    void SetDefaultSandboxEnabled(bool v);
    void SetDefaultFollowClose(bool v);
    void SetUseKeybinds(bool v);
    void SetNotificationsEnabled(bool v);
    void SetFollowDistance(const std::string& preset);

    // Gate all Horde notifications through this — respects the mute setting
    static void Notify(const char* msg);

private:
    Settings() = default;
    std::string GetConfigPath() const;

    uint32_t _modifierKey = 0x2A;        // Left Shift
    uint32_t _activateKey = 0x23;        // H
    uint32_t _groupModifierKey = 0x38;   // Left Alt
    uint32_t _followAllKey = 0x21;       // F
    uint32_t _waitAllKey = 0x11;         // W
    uint32_t _summonAllKey = 0x1F;       // S
    uint32_t _passiveAllKey = 0x19;      // P
    static constexpr int kSettingsVersion = 3;
    int      _settingsVersion = kSettingsVersion;
    static constexpr int kMaxFollowerCeiling = 20;
    int      _maxFollowers = 20;
    bool     _defaultSandboxEnabled = false;
    bool     _defaultFollowClose = false;
    bool     _useKeybinds = false;
    bool     _notificationsEnabled = true;
    std::string _followDistance = "normal";  // "close", "normal", "far"
};
