#pragma once
#include "pch.h"
#include "MeridianUIAPI/ViewAPI.h"

class HordeUI
{
public:
    static HordeUI& GetSingleton();

    void Initialize();
    void Toggle();
    void RefreshState();

    bool IsOpen() const { return _isOpen; }

private:
    HordeUI() = default;

    void RegisterListeners();
    void PushStateToView();
    void PushDismissedToView();
    std::string BuildStateJSON() const;
    std::string BuildDismissedJSON() const;

    Meridian::UI::View::ViewHandle _view = Meridian::UI::View::INVALID_VIEW_HANDLE;
    bool       _isOpen = false;
};
