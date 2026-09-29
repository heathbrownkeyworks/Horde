#include "HordeMenu.h"
#include "ImGuiHost.h"
namespace Horde::ImGuiUI
{
HordeMenu::HordeMenu()
{
    using F = RE::UI_MENU_FLAGS;
    menuFlags.set(F::kPausesGame, F::kUsesCursor, F::kUpdateUsesCursor, F::kUsesMenuContext, F::kCustomRendering,
                  F::kDisablePauseMenu);
    inputContext.set(RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode);
    depthPriority = 11;
}
bool HordeMenu::Register()
{
    if (auto *ui = RE::UI::GetSingleton())
    {
        ui->Register(MENU_NAME, Create);
        return true;
    }
    return false;
}
RE::IMenu *HordeMenu::Create()
{
    return new HordeMenu();
}
void HordeMenu::PostDisplay()
{
    ImGuiHost::GetSingleton().DrawFrame();
}
RE::UI_MESSAGE_RESULTS HordeMenu::ProcessMessage(RE::UIMessage &message)
{
    switch (*message.type)
    {
    case RE::UI_MESSAGE_TYPE::kShow:
        ImGuiHost::GetSingleton().OnShown();
        break;
    case RE::UI_MESSAGE_TYPE::kHide:
    case RE::UI_MESSAGE_TYPE::kForceHide:
        ImGuiHost::GetSingleton().OnHidden();
        break;
    case RE::UI_MESSAGE_TYPE::kUserEvent:
    case RE::UI_MESSAGE_TYPE::kScaleformEvent:
        // Back is handled in the native screen, including safe modal cancel.
        return RE::UI_MESSAGE_RESULTS::kHandled;
    default:
        break;
    }
    return RE::IMenu::ProcessMessage(message);
}
} // namespace Horde::ImGuiUI
