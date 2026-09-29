#include "InputDispatchHook.h"
#include "InputRouting.h"
#include "ImGuiHost.h"
#include "RE/D/DeviceConnectEvent.h"
#include "RE/T/ThumbstickEvent.h"
#include <atomic>
#include <mutex>

namespace Horde::ImGuiUI
{
namespace
{
bool forwarding = false; // Skyrim polls and dispatches input on its main thread.
std::atomic<bool> installed{false};

struct DispatchInput
{
    static void thunk(RE::BSTEventSource<RE::InputEvent *> *source, RE::InputEvent *const *events)
    {
        if (!events || !*events)
        {
            original(source, events);
            return;
        }
        static input::InputOwnership ownership;
        auto &host = ImGuiHost::GetSingleton();
        const bool owned = host.OwnsInput();
        input::RouteInputBatch(
            *events,
            [&](const RE::InputEvent &event) {
                using Device = input::Device;
                const auto device = event.GetDevice();
                Device kind;
                if (device == RE::INPUT_DEVICE::kKeyboard)
                    kind = Device::Keyboard;
                else if (device == RE::INPUT_DEVICE::kMouse)
                    kind = Device::Mouse;
                else if (device == RE::INPUT_DEVICE::kGamepad)
                    kind = Device::Gamepad;
                else
                    return false;
                if (event.eventType == RE::INPUT_EVENT_TYPE::kDeviceConnect)
                {
                    if (!static_cast<const RE::DeviceConnectEvent &>(event).connected)
                        ownership.Disconnect(kind);
                    return false;
                }
                if (event.eventType == RE::INPUT_EVENT_TYPE::kChar)
                    return input::InputOwnership::HideCharacter(owned);
                if (event.eventType == RE::INPUT_EVENT_TYPE::kThumbstick)
                {
                    const auto &stick = static_cast<const RE::ThumbstickEvent &>(event);
                    return input::InputOwnership::HideStick(owned, stick.xValue, stick.yValue);
                }
                if (event.eventType != RE::INPUT_EVENT_TYPE::kButton)
                    return false; // native mouse motion, connection state
                const auto *button = event.AsButtonEvent();
                using Phase = input::Phase;
                return ownership.HideButton(owned, kind, button->GetIDCode(),
                                            button->IsDown() ? Phase::Down
                                            : button->IsUp() ? Phase::Up
                                                             : Phase::Held);
            },
            [&](RE::InputEvent *head) {
                RE::InputEvent *const full[]{head};
                host.ProcessEvent(full, source);
            },
            [&](RE::InputEvent *head) {
                RE::InputEvent *const visible[]{head};
                original(source, visible);
            },
            forwarding);
    }
    static inline REL::Relocation<decltype(thunk)> original;
};
} // namespace

bool IsForwardingInput()
{
    return forwarding;
}
bool IsInputDispatchHookInstalled()
{
    return installed.load();
}

bool InstallInputDispatchHook()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // Preserve the existing call target so compatible input hooks remain chained.
        const auto call = REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B);
        if (*reinterpret_cast<const std::uint8_t *>(call) != 0xE8)
        {
            logger::error(
                "Horde input dispatch guard cannot hook this runtime/call site; native menu opening is disabled");
            return;
        }
        DispatchInput::original = SKSE::GetTrampoline().write_call<5>(call, DispatchInput::thunk);
        installed = true;
        logger::info("Horde keyboard/mouse/controller input dispatch guard installed");
    });
    return installed.load();
}
} // namespace Horde::ImGuiUI
