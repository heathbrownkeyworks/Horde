#pragma once
#include "pch.h"

// Lightweight entry for the persistent follower registry.
// Tracks every follower who has ever been in Horde, even after dismissal.
struct RegistryEntry {
    std::string name;
    RE::FormID  homeWorldspace = 0;
    float       homeX = 0.0f, homeY = 0.0f, homeZ = 0.0f;
    std::string homeName;

    // Cached original editor location, captured once on first recruitment.
    // Used to restore the vanilla editorLocForm/Coord/Rot if the user clears
    // a Horde home. 0 = not captured (or actor had no editor location).
    RE::FormID  originalEditorLocFormID = 0;
    float       originalEditorLocX = 0.0f;
    float       originalEditorLocY = 0.0f;
    float       originalEditorLocZ = 0.0f;
    float       originalEditorLocRot = 0.0f;
};

inline void to_json(nlohmann::json& j, const RegistryEntry& e) {
    j = {
        {"name", e.name},
        {"homeWorldspace", e.homeWorldspace},
        {"homeX", e.homeX},
        {"homeY", e.homeY},
        {"homeZ", e.homeZ},
        {"homeName", e.homeName},
        {"originalEditorLocFormID", e.originalEditorLocFormID},
        {"originalEditorLocX", e.originalEditorLocX},
        {"originalEditorLocY", e.originalEditorLocY},
        {"originalEditorLocZ", e.originalEditorLocZ},
        {"originalEditorLocRot", e.originalEditorLocRot}
    };
}

inline void from_json(const nlohmann::json& j, RegistryEntry& e) {
    j.at("name").get_to(e.name);
    e.homeWorldspace = j.value("homeWorldspace", static_cast<RE::FormID>(0));
    e.homeX = j.value("homeX", 0.0f);
    e.homeY = j.value("homeY", 0.0f);
    e.homeZ = j.value("homeZ", 0.0f);
    e.homeName = j.value("homeName", std::string(""));
    e.originalEditorLocFormID = j.value("originalEditorLocFormID", static_cast<RE::FormID>(0));
    e.originalEditorLocX = j.value("originalEditorLocX", 0.0f);
    e.originalEditorLocY = j.value("originalEditorLocY", 0.0f);
    e.originalEditorLocZ = j.value("originalEditorLocZ", 0.0f);
    e.originalEditorLocRot = j.value("originalEditorLocRot", 0.0f);
}

struct FollowerData {
    RE::FormID  actorFormID = 0;
    std::string name;
    std::string className;
    int         level = 1;
    float       originalAggression = 1.0f;

    bool        isSandboxEnabled = false;
    bool        isPassive = false;
    bool        isWaiting = false;
    bool        isFollowClose = false;

    // True (default) = Horde forces actor essential while tracked.
    // False = user toggled essential off (e.g. for Boethiah's Calling).
    // On dismissal, the actor is always restored to originalProtection
    // regardless of this flag.
    bool        isEssential = true;

    // Original protection level: 0=mortal, 1=protected, 2=essential
    int         originalProtection = 0;

    // Alias slot in Horde_FollowerQuest (0-19, or -1 if unassigned)
    int         aliasSlot = -1;

    // Home location (0 = no home assigned)
    RE::FormID  homeWorldspace = 0;
    float       homeX = 0.0f;
    float       homeY = 0.0f;
    float       homeZ = 0.0f;
    std::string homeName;  // display name, e.g. "Breezehome"
};

inline void to_json(nlohmann::json& j, const FollowerData& d) {
    j = {
        {"formID", d.actorFormID},
        {"name", d.name},
        {"className", d.className},
        {"level", d.level},
        {"originalAggression", d.originalAggression},
        {"isSandboxEnabled", d.isSandboxEnabled},
        {"isPassive", d.isPassive},
        {"isWaiting", d.isWaiting},
        {"isFollowClose", d.isFollowClose},
        {"isEssential", d.isEssential},
        {"originalProtection", d.originalProtection},
        {"aliasSlot", d.aliasSlot},
        {"homeWorldspace", d.homeWorldspace},
        {"homeX", d.homeX},
        {"homeY", d.homeY},
        {"homeZ", d.homeZ},
        {"homeName", d.homeName}
    };
}

inline void from_json(const nlohmann::json& j, FollowerData& d) {
    j.at("formID").get_to(d.actorFormID);
    j.at("name").get_to(d.name);
    d.className = j.value("className", std::string(""));
    d.level = j.value("level", 1);
    d.originalAggression = j.value("originalAggression", 1.0f);
    d.isSandboxEnabled = j.value("isSandboxEnabled", false);
    d.isPassive = j.value("isPassive", false);
    d.isWaiting = j.value("isWaiting", false);
    d.isFollowClose = j.value("isFollowClose", false);
    d.isEssential = j.value("isEssential", true);
    d.originalProtection = j.value("originalProtection", 0);
    d.aliasSlot = j.value("aliasSlot", -1);
    d.homeWorldspace = j.value("homeWorldspace", static_cast<RE::FormID>(0));
    d.homeX = j.value("homeX", 0.0f);
    d.homeY = j.value("homeY", 0.0f);
    d.homeZ = j.value("homeZ", 0.0f);
    d.homeName = j.value("homeName", std::string(""));
}
