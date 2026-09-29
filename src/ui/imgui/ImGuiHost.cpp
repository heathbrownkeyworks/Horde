#include "ImGuiHost.h"
#include "HordeMenu.h"
#include "InputMap.h"
#include "InputDispatchHook.h"
#include "Settings.h"
#include "ui/HordeUI.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <backends/imgui_impl_dx11.h>
#include <imgui_internal.h>
#include "RE/R/Renderer.h"
#include "RE/D/DeviceConnectEvent.h"
#include "RE/M/MouseMoveEvent.h"
#include "RE/M/MenuCursor.h"
#include "RE/T/ThumbstickEvent.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <cmath>

namespace Horde::ImGuiUI
{
namespace
{
struct ContextScope
{
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ~ContextScope()
    {
        ImGui::SetCurrentContext(previous);
    }
};
ImGuiKey GamepadKey(std::uint32_t id)
{
    switch (id)
    {
    case 1:
        return ImGuiKey_GamepadDpadUp;
    case 2:
        return ImGuiKey_GamepadDpadDown;
    case 4:
        return ImGuiKey_GamepadDpadLeft;
    case 8:
        return ImGuiKey_GamepadDpadRight;
    case 0x10:
        return ImGuiKey_GamepadStart;
    case 0x20:
        return ImGuiKey_GamepadBack;
    case 0x40:
        return ImGuiKey_GamepadL3;
    case 0x80:
        return ImGuiKey_GamepadR3;
    case 0x100:
        return ImGuiKey_GamepadL1;
    case 0x200:
        return ImGuiKey_GamepadR1;
    case 0x1000:
        return ImGuiKey_GamepadFaceDown;
    case 0x2000:
        return ImGuiKey_GamepadFaceRight;
    case 0x4000:
        return ImGuiKey_GamepadFaceLeft;
    case 0x8000:
        return ImGuiKey_GamepadFaceUp;
    default:
        return ImGuiKey_None;
    }
}
float Stick(float value)
{
    return std::copysign(std::clamp((std::abs(value) - 0.25f) / 0.75f, 0.0f, 1.0f), value);
}
} // namespace
struct ImGuiHost::State
{
    struct Input
    {
        enum Kind
        {
            Key,
            Button,
            Wheel
        } kind;
        int code = 0;
        bool down = false;
        float x = 0, y = 0;
    };
    std::atomic<bool> desired{false}, active{false}, closing{false}, closeQueued{false}, clearInput{true};
    std::atomic<bool> usingGamepad{false}, cursorMode{false}, armed{false};
    std::atomic<std::uint32_t> held{0}, suppressed{0}, selected{0};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<float> leftX{0}, leftY{0}, rightX{0}, rightY{0};
    std::atomic<HWND> window{nullptr};
    bool registered = false, shown = false, ready = false;
    bool controllerMouseDown = false;
    std::mutex inputLock, modelLock;
    std::vector<Input> pending;
    Model model = Model::object();
    Fonts fonts;
    ScreenState screen;
    ImGuiContext *context = nullptr;
    std::uint64_t renderGeneration = 0;
    std::chrono::steady_clock::time_point lastFrame{};
    void SetInputMode(bool gamepad, bool cursor, std::uint32_t freshButton = 0)
    {
        // A held button must not become a fresh click/activation when its
        // meaning changes between native navigation and pointer mode.
        if (usingGamepad != gamepad || cursorMode != cursor)
            suppressed.fetch_or(held.load() & ~freshButton);
        usingGamepad = gamepad;
        cursorMode = cursor;
    }
};
ImGuiHost::ImGuiHost() : _state(std::make_unique<State>()) {}
// The game's D3D device may already be gone during static destruction.
ImGuiHost::~ImGuiHost() = default;
ImGuiHost &ImGuiHost::GetSingleton()
{
    static ImGuiHost value;
    return value;
}
void ImGuiHost::Initialize()
{
    auto &s = *_state;
    if (s.registered)
        return;
    auto *input = RE::BSInputDeviceManager::GetSingleton();
    auto *ui = RE::UI::GetSingleton();
    if (!input || !ui || !HordeMenu::Register())
    {
        logger::error("Horde native menu registration failed");
        return;
    }
    input->AddEventSink(this);
    ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
    s.registered = true;
    logger::info("Horde native ImGui menu registered; controller access through the Horde lesser power");
}
bool ImGuiHost::RequestOpen(std::uint32_t selected)
{
    auto &s = *_state;
    if (!s.registered || s.closing || !IsInputDispatchHookInstalled())
        return false;
    if (s.desired)
        return true;
    auto *ui = RE::UI::GetSingleton();
    auto *q = RE::UIMessageQueue::GetSingleton();
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!ui || !q || !player || !player->Is3DLoaded() || ui->GameIsPaused() || !ui->IsShowingMenus())
        return false;
    for (const auto &menu : ui->menuStack)
        if (menu && (menu->UsesMenuContext() || menu->PausesGame()))
            return false;
    s.selected = selected;
    ++s.generation;
    s.closeQueued = false;
    s.clearInput = true;
    s.desired = true;
    s.cursorMode = false;
    s.armed = false;
    q->AddMessage(HordeMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    return true;
}
void ImGuiHost::RequestClose()
{
    auto &s = *_state;
    const bool requested = s.desired.exchange(false);
    s.active = false;
    s.clearInput = true;
    s.held = 0;
    s.suppressed = 0;
    s.leftX = 0;
    s.leftY = 0;
    s.rightX = 0;
    s.rightY = 0;
    if (s.closing || (!requested && !s.shown))
        return;
    ++s.generation;
    if (auto *q = RE::UIMessageQueue::GetSingleton())
    {
        s.closing = true;
        q->AddMessage(HordeMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }
}
bool ImGuiHost::IsOpen() const
{
    return _state->desired.load();
}
std::uint64_t ImGuiHost::Generation() const
{
    return _state->generation.load();
}
bool ImGuiHost::HasFocus() const
{
    const auto &s = *_state;
    const auto window = s.window.load();
    if (!s.desired || !s.active || (window && GetForegroundWindow() != window))
        return false;
    const auto *ui = RE::UI::GetSingleton();
    return ui && !ui->closingAllMenus;
}
bool ImGuiHost::OwnsInput() const
{
    const auto &s = *_state;
    const auto window = s.window.load();
    // Include queued show/hide transitions, so an opening shortcut cannot leak
    // while the UI thread is adding or removing the menu.
    return (s.desired || s.active || s.closing) && (!window || GetForegroundWindow() == window);
}
void ImGuiHost::OnShown()
{
    auto &s = *_state;
    if (s.shown)
        return;
    s.shown = true;
    if (!s.desired || s.closing)
    {
        RequestClose();
        return;
    }
    s.active = true;
    s.clearInput = true;
}
void ImGuiHost::OnHidden()
{
    auto &s = *_state;
    s.active = false;
    s.desired = false;
    s.shown = false;
    s.closing = false;
    s.clearInput = true;
    s.held = 0;
    s.suppressed = 0;
    s.leftX = 0;
    s.leftY = 0;
    s.rightX = 0;
    s.rightY = 0;
    ++s.generation;
}
void ImGuiHost::QueueClose()
{
    auto &s = *_state;
    if (s.closeQueued.exchange(true))
        return;
    const auto generation = s.generation.load();
    SKSE::GetTaskInterface()->AddTask([this, generation] {
        if (Generation() == generation)
            RequestClose();
    });
}
void ImGuiHost::Publish(Model model)
{
    std::scoped_lock lock(_state->modelLock);
    _state->model = std::move(model);
}
bool ImGuiHost::InitializeRenderer()
{
    auto &s = *_state;
    if (s.ready)
        return true;
    auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer)
        return false;
    auto &data = renderer->GetRuntimeData();
    auto *device = reinterpret_cast<ID3D11Device *>(data.forwarder);
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(data.context);
    auto *swapchain = reinterpret_cast<IDXGISwapChain *>(data.renderWindows[0].swapChain);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!device || !context || !swapchain || FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow)
        return false;
    IMGUI_CHECKVERSION();
    s.context = ImGui::CreateContext();
    ImGui::SetCurrentContext(s.context);
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    s.context->ConfigNavWindowingWithGamepad = false;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    // Input comes from Skyrim events; the host does not poll devices.
    io.MouseDrawCursor = false; // Skyrim draws the native cursor.
    if (!LoadFonts(io, "Data/SKSE/Plugins/Horde/fonts", s.fonts))
        logger::warn("Horde native fonts missing; using fallback fonts");
    if (!ImGui_ImplDX11_Init(device, context))
    {
        ImGui::DestroyContext(s.context);
        s.context = nullptr;
        s.fonts = {};
        return false;
    }
    s.window = desc.OutputWindow;
    s.ready = true;
    s.lastFrame = std::chrono::steady_clock::now();
    logger::info("Horde ImGui {} initialized with Skyrim DX11 device", IMGUI_VERSION);
    return true;
}
void ImGuiHost::DrawFrame()
{
    auto &s = *_state;
    if (!s.active)
        return;
    ContextScope scope;
    if (!HasFocus())
    {
        QueueClose();
        return;
    }
    if (!InitializeRenderer())
    {
        logger::error("Horde native renderer unavailable; closing menu");
        QueueClose();
        return;
    }
    ImGui::SetCurrentContext(s.context);
    auto &io = ImGui::GetIO();
    const auto generation = s.generation.load();
    RECT rect{};
    if (!GetClientRect(s.window, &rect) || rect.right <= 0 || rect.bottom <= 0)
    {
        QueueClose();
        return;
    }
    io.DisplaySize = {static_cast<float>(rect.right), static_cast<float>(rect.bottom)};
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - s.lastFrame).count(), 0.001f, 0.10f);
    s.lastFrame = now;
    if (s.renderGeneration != generation)
    {
        s.screen = {};
        s.screen.selected = s.selected;
        s.renderGeneration = generation;
        s.clearInput = true;
    }
    if (s.clearInput.exchange(false))
    {
        io.ClearInputKeys();
        io.ClearInputMouse();
        io.ClearEventsQueue();
        s.controllerMouseDown = false;
        std::scoped_lock lock(s.inputLock);
        s.pending.clear();
    }
    // A button held while casting the power must not become a menu command.
    if (s.held.load() == 0)
        s.armed = true;
    const bool gamepad = s.usingGamepad.load(), cursor = s.cursorMode.load();
    if (gamepad && !cursor)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    ImGui_ImplDX11_NewFrame();
    std::vector<State::Input> inputs;
    {
        std::scoped_lock lock(s.inputLock);
        inputs.swap(s.pending);
    }
    if ((!gamepad || !cursor) && s.controllerMouseDown)
    {
        // Release a controller click before applying fresh physical mouse input.
        io.AddMouseButtonEvent(0, false);
        s.controllerMouseDown = false;
    }
    for (const auto &event : inputs)
    {
        switch (event.kind)
        {
        case State::Input::Key:
            io.AddKeyEvent(static_cast<ImGuiKey>(event.code), event.down);
            break;
        case State::Input::Button:
            io.AddMouseButtonEvent(event.code, event.down);
            break;
        case State::Input::Wheel:
            io.AddMouseWheelEvent(0, event.y);
            break;
        }
    }
    // Rebuild controller state each frame. Switching input source or cursor
    // mode releases the old action even if A remains physically held.
    const auto buttons = s.armed && gamepad ? s.held.load() & ~s.suppressed.load() : 0u;
    for (std::uint32_t bit = 1; bit <= 0x8000; bit <<= 1)
    {
        const auto key = GamepadKey(bit);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, (buttons & bit) && !(cursor && bit == 0x1000));
    }
    const bool click = cursor && (buttons & 0x1000);
    if (click != s.controllerMouseDown)
    {
        io.AddMouseButtonEvent(0, click);
        s.controllerMouseDown = click;
    }
    if (s.armed)
    {
        const float lx = Stick(s.leftX), ly = Stick(s.leftY), rx = Stick(s.rightX), ry = Stick(s.rightY);
        const bool navigate = gamepad && !cursor;
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, navigate && lx < 0, navigate ? std::max(-lx, 0.0f) : 0);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, navigate && lx > 0, navigate ? std::max(lx, 0.0f) : 0);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, navigate && ly > 0, navigate ? std::max(ly, 0.0f) : 0);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, navigate && ly < 0, navigate ? std::max(-ly, 0.0f) : 0);
        if (gamepad && cursor)
        {
            if (auto *nativeCursor = RE::MenuCursor::GetSingleton())
            {
                auto &position = nativeCursor->GetRuntimeData();
                const float speed = 900 * (io.DisplaySize.y / 1080.0f) * std::min(io.DeltaTime, 0.05f);
                position.cursorPosX = std::clamp(position.cursorPosX + rx * speed, 0.0f, io.DisplaySize.x - 1);
                position.cursorPosY = std::clamp(position.cursorPosY - ry * speed, 0.0f, io.DisplaySize.y - 1);
            }
        }
        else if (gamepad && ry != 0)
        {
            // Focus scrolling is targeted to the active native child window.
            s.screen.rightScroll = -ry * 650 * io.DeltaTime;
        }
    }
    if (auto *nativeCursor = RE::MenuCursor::GetSingleton())
    {
        const auto &position = nativeCursor->GetRuntimeData();
        io.AddMousePosEvent(position.cursorPosX, position.cursorPosY);
    }
    Model model;
    {
        std::scoped_lock lock(s.modelLock);
        model = s.model;
    }
    model["_usingGamepad"] = gamepad;
    model["_controllerCursor"] = cursor;
    model["_controllerGlyphs"] = Settings::GetSingleton().GetControllerGlyphs();
    ImGui::NewFrame();
    auto result = DrawHorde(model, s.screen, s.fonts, HasFocus());
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!HasFocus() || s.generation != generation)
        return;
    for (auto &action : result.actions)
        HordeUI::GetSingleton().Dispatch(std::move(action), generation);
}
RE::BSEventNotifyControl ImGuiHost::ProcessEvent(RE::InputEvent *const *events, RE::BSTEventSource<RE::InputEvent *> *)
{
    if (IsForwardingInput())
        return RE::BSEventNotifyControl::kContinue;
    auto &s = *_state;
    if (s.active && !HasFocus())
        QueueClose();
    if (!events)
        return RE::BSEventNotifyControl::kContinue;
    std::vector<State::Input> batch;
    for (auto *event = *events; event; event = event->next)
    {
        if (event->eventType == RE::INPUT_EVENT_TYPE::kDeviceConnect &&
            event->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
            !static_cast<const RE::DeviceConnectEvent *>(event)->connected)
        {
            s.held = 0;
            s.suppressed = 0;
            s.leftX = 0;
            s.leftY = 0;
            s.rightX = 0;
            s.rightY = 0;
            s.usingGamepad = false;
            s.cursorMode = false;
            s.clearInput = true;
        }
        if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove && HasFocus())
        {
            const auto *move = event->AsMouseMoveEvent();
            if (move && (move->mouseInputX || move->mouseInputY))
            {
                s.SetInputMode(false, false);
            }
        }
        if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick)
        {
            const auto *stick = event->AsThumbstickEvent();
            if (!stick)
                continue;
            if (stick->IsLeft())
            {
                s.leftX = stick->xValue;
                s.leftY = stick->yValue;
            }
            else
            {
                s.rightX = stick->xValue;
                s.rightY = stick->yValue;
            }
            if (std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f)
                s.SetInputMode(true, s.cursorMode);
        }
        if (event->eventType != RE::INPUT_EVENT_TYPE::kButton)
            continue;
        const auto *button = event->AsButtonEvent();
        if (!button || (!button->IsDown() && !button->IsUp()))
            continue;
        const auto id = button->GetIDCode();
        const bool down = button->IsDown();
        if (button->GetDevice() == RE::INPUT_DEVICE::kGamepad)
        {
            if (down)
                s.SetInputMode(true, s.cursorMode, input::IsDigitalControllerButton(id) ? id : 0);
            if (input::IsDigitalControllerButton(id))
            {
                if (down)
                    s.held.fetch_or(id);
                else
                {
                    s.held.fetch_and(~id);
                    s.suppressed.fetch_and(~id);
                }
            }
            if (!HasFocus() || !s.armed)
                continue;
            if (down && id == 0x80)
            {
                s.SetInputMode(true, !s.cursorMode.load(), id);
                s.clearInput = true;
                continue;
            }
            if (down && (id == 1 || id == 2 || id == 4 || id == 8) && s.cursorMode)
            {
                s.SetInputMode(true, false, id);
                s.clearInput = true;
            }
            // Keep both edges of taps that occur between two rendered frames;
            // the held-state submission also releases actions on mode changes.
            if (!down || !(s.suppressed.load() & id))
            {
                if (s.cursorMode && id == 0x1000)
                    batch.push_back({State::Input::Button, 0, down});
                else if (const auto key = GamepadKey(id); key != ImGuiKey_None)
                    batch.push_back({State::Input::Key, static_cast<int>(key), down});
            }
            continue;
        }
        if (!HasFocus())
            continue;
        if (down)
        {
            s.SetInputMode(false, false);
        }
        if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
        {
            const auto key = input::ScanCodeToImGuiKey(id);
            if (key != ImGuiKey_None)
                batch.push_back({State::Input::Key, static_cast<int>(key), down});
            if (id == 0x2a || id == 0x36)
                batch.push_back({State::Input::Key, ImGuiMod_Shift, down});
        }
        else if (button->GetDevice() == RE::INPUT_DEVICE::kMouse)
        {
            if (id <= 2)
                batch.push_back({State::Input::Button, static_cast<int>(id), down});
            else if (down && (id == 8 || id == 9))
                batch.push_back({State::Input::Wheel, 0, true, 0, id == 8 ? 1.0f : -1.0f});
        }
    }
    if (!batch.empty())
    {
        std::scoped_lock lock(s.inputLock);
        s.pending.insert(s.pending.end(), batch.begin(), batch.end());
    }
    return RE::BSEventNotifyControl::kContinue;
}
RE::BSEventNotifyControl ImGuiHost::ProcessEvent(const RE::MenuOpenCloseEvent *event,
                                                 RE::BSTEventSource<RE::MenuOpenCloseEvent> *)
{
    if (!event || !event->opening || !IsOpen() || event->menuName == HordeMenu::MENU_NAME)
        return RE::BSEventNotifyControl::kContinue;
    auto *ui = RE::UI::GetSingleton();
    auto menu = ui ? ui->GetMenu(event->menuName) : nullptr;
    if (event->menuName == RE::LoadingMenu::MENU_NAME || event->menuName == RE::MainMenu::MENU_NAME ||
        (menu && (menu->UsesMenuContext() || menu->PausesGame())))
    {
        _state->active = false;
        QueueClose();
    }
    return RE::BSEventNotifyControl::kContinue;
}
} // namespace Horde::ImGuiUI
