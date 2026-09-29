#pragma once
namespace Horde::ImGuiUI
{
class HordeMenu final : public RE::IMenu
{
  public:
    static constexpr std::string_view MENU_NAME = "HordeNativeMenu";
    static bool Register();
    static RE::IMenu *Create();
    void PostDisplay() override;
    RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage &) override;

  private:
    HordeMenu();
};
} // namespace Horde::ImGuiUI
