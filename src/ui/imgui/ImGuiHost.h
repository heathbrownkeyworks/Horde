#pragma once
#include "HordeScreen.h"
#include <memory>

namespace Horde::ImGuiUI
{
class ImGuiHost final : public RE::BSTEventSink<RE::InputEvent *>, public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
  public:
    static ImGuiHost &GetSingleton();
    void Initialize();
    bool RequestOpen(std::uint32_t selected);
    void RequestClose();
    bool IsOpen() const;
    bool HasFocus() const;
    bool OwnsInput() const;
    std::uint64_t Generation() const;
    void Publish(Model);
    void DrawFrame();
    void OnShown();
    void OnHidden();
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent *const *, RE::BSTEventSource<RE::InputEvent *> *) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent *,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override;

  private:
    ImGuiHost();
    ~ImGuiHost() override;
    struct State;
    std::unique_ptr<State> _state;
    bool InitializeRenderer();
    void QueueClose();
};
} // namespace Horde::ImGuiUI
