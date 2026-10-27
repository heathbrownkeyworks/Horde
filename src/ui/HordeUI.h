#pragma once
#include "pch.h"
#include <functional>
#include <unordered_map>

namespace MAGELIGHT_API
{
struct EventData;
struct MagelightApi4;
} // namespace MAGELIGHT_API

class HordeUI
{
  public:
    static HordeUI &GetSingleton();

    void Initialize();
    void Toggle();
    void Close();
    void RefreshState();

    bool IsOpen() const { return _open; }

    // Horde's interface is a web page hosted by Magelight UI. Without it the
    // menu cannot open; orders through powers and hotkeys still work.
    bool HasHost() const { return _magelight != nullptr; }

  private:
    HordeUI() = default;

    void RegisterListeners();
    bool InitializeMagelight();
    void Open();
    void ExecuteScript(const std::string &script);
    void PushStateToView();
    void PushDismissedToView();
    std::string BuildStateJSON() const;
    std::string BuildDismissedJSON() const;

    // Magelight callbacks. Listeners run on the render thread mid-frame; events
    // and posted commands run on the game thread.
    static void OnMagelightListener(std::uint64_t view, const char *argument, void *user);
    static void RunMagelightCommand(void *user);
    static void OnMagelightEvent(const MAGELIGHT_API::EventData *event, void *user);

    std::unordered_map<std::string, std::function<void(const char *)>> _handlers;
    std::uint64_t _view = 0;  // MAGELIGHT_API::ViewId
    const MAGELIGHT_API::MagelightApi4 *_magelight = nullptr;
    std::uint32_t _magelightMod = 0;  // MAGELIGHT_API::ModId
    std::string _language = "en";  // BCP 47 tag for the page
    bool _open = false;
};
