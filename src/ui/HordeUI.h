#pragma once
#include "pch.h"
#include "ui/imgui/HordeScreen.h"
#include <functional>
#include <unordered_map>

class HordeUI
{
  public:
    static HordeUI &GetSingleton();

    void Initialize();
    void Toggle();
    void Close();
    void RefreshState();

    bool IsOpen() const;
    void Dispatch(Horde::ImGuiUI::Action action, std::uint64_t generation);

  private:
    HordeUI() = default;

    void RegisterListeners();
    void PushStateToView();
    void PushDismissedToView();
    std::string BuildStateJSON() const;
    std::string BuildDismissedJSON() const;

    std::unordered_map<std::string, std::function<void(const char *)>> _handlers;
};
