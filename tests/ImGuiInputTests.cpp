#include "ui/imgui/InputRouting.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Horde::ImGuiUI::input;
namespace
{
int assertions = 0;
void Check(bool value, const std::string &message)
{
    ++assertions;
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
struct Event
{
    Event *next{};
    Device device = Device::Keyboard;
    std::uint32_t code{};
    Phase phase = Phase::Down;
    bool motion = false;
};
int Count(Event *event)
{
    int count = 0;
    for (; event; event = event->next)
        ++count;
    return count;
}
} // namespace
int main()
{
    for (auto device : {Device::Keyboard, Device::Mouse, Device::Gamepad})
    {
        InputOwnership ownership;
        Check(!ownership.HideButton(false, device, 1, Phase::Down), "gameplay down passes");
        Check(ownership.HideButton(true, device, 1, Phase::Held), "pre-held repetition blocked");
        Check(!ownership.HideButton(true, device, 1, Phase::Up), "pre-held release balances outside listener");
        Check(ownership.HideButton(true, device, 1, Phase::Down), "owned down hidden");
        Check(ownership.HideButton(false, device, 1, Phase::Held), "owned hold stays hidden after close");
        Check(ownership.HideButton(false, device, 1, Phase::Up), "owned release stays hidden after close");
        Check(!ownership.HideButton(false, device, 1, Phase::Down), "new press after close passes");
        ownership.HideButton(true, device, 2, Phase::Down);
        ownership.Disconnect(device);
        Check(!ownership.HideButton(false, device, 2, Phase::Up), "disconnect clears device ownership");
    }
    {
        InputOwnership ownership;
        for (auto phase : {Phase::Down, Phase::Held, Phase::Up})
            Check(!ownership.HideButton(true, Device::Keyboard, InputOwnership::PrintScreen, phase),
                  "Skyrim screenshot key remains available");
        ownership.HideButton(true, Device::Gamepad, 9, Phase::Down);
        Check(!ownership.HideButton(false, Device::Gamepad, 1, Phase::Up), "trigger is not a d-pad bitmask");
        Check(!ownership.HideButton(false, Device::Gamepad, 10, Phase::Up), "triggers have separate ownership");
        Check(!ownership.HideButton(false, Device::Mouse, 9, Phase::Up), "devices have separate ownership");
        Check(ownership.HideButton(false, Device::Gamepad, 9, Phase::Up), "trigger release survives close");
        Check(InputOwnership::HideCharacter(true) && !InputOwnership::HideCharacter(false), "text ownership");
        Check(InputOwnership::HideStick(true, .5f, 0), "owned stick motion hidden");
        Check(!InputOwnership::HideStick(true, 0, 0), "neutral stick sample clears downstream axes");
        Check(!InputOwnership::HideStick(false, .5f, 0), "gameplay stick motion passes");
    }
    {
        // A simulated mod attempts to open on keyboard, mouse and pad presses.
        Event end{nullptr, Device::Gamepad, 0x1000};
        Event motion{&end, Device::Mouse, 0, Phase::Down, true};
        Event mouse{&motion, Device::Mouse, 1};
        Event keyboard{&mouse, Device::Keyboard, 0x23};
        InputOwnership ownership;
        bool forwarding = false;
        int ownerCalls = 0, modOpens = 0, nativeMoves = 0;
        RouteInputBatch(
            &keyboard,
            [&](const Event &event) {
                return !event.motion && ownership.HideButton(true, event.device, event.code, event.phase);
            },
            [&](Event *head) {
                ++ownerCalls;
                Check(!forwarding && head == &keyboard && Count(head) == 4,
                      "owner sees original batch before filtering");
            },
            [&](Event *head) {
                Check(forwarding, "owner sink skips duplicate delivery while forwarding");
                for (auto *event = head; event; event = event->next)
                {
                    if (event->motion)
                        ++nativeMoves;
                    else
                        ++modOpens;
                }
            },
            forwarding);
        Check(ownerCalls == 1 && modOpens == 0, "other listeners cannot open a mod from owned buttons");
        Check(nativeMoves == 1, "native mouse movement reaches Skyrim cursor");
        Check(!forwarding && keyboard.next == &mouse && mouse.next == &motion && motion.next == &end && !end.next,
              "engine input chain and forwarding guard restored");
        // The menu closes before the three buttons are released. None may leak
        // to another mod's key-up shortcut, or be replayed on reopening.
        keyboard.phase = mouse.phase = end.phase = Phase::Up;
        RouteInputBatch(
            &keyboard,
            [&](const Event &event) {
                return !event.motion && ownership.HideButton(false, event.device, event.code, event.phase);
            },
            [](Event *) {},
            [&](Event *head) { Check(head == &motion && !head->next, "close does not leak owned releases"); },
            forwarding);
        keyboard.phase = mouse.phase = end.phase = Phase::Down;
        RouteInputBatch(
            &keyboard,
            [&](const Event &event) {
                return !event.motion && ownership.HideButton(false, event.device, event.code, event.phase);
            },
            [](Event *) { Check(false, "unfiltered batch uses normal sink delivery"); },
            [&](Event *head) {
                Check(!forwarding && head == &keyboard && Count(head) == 4, "all input restored after close");
            },
            forwarding);
    }
    {
        Event last, first{&last};
        bool forwarding = false;
        RouteInputBatch(
            &first, [](const Event &) { return true; }, [](Event *) {},
            [&](Event *head) { Check(!head && forwarding, "fully hidden batch forwards a null head"); }, forwarding);
        Check(first.next == &last && !last.next && !forwarding, "empty batch restores links");
        try
        {
            RouteInputBatch(
                &first, [](const Event &) { return true; }, [](Event *) {},
                [](Event *) { throw std::runtime_error("listener failure"); }, forwarding);
        }
        catch (const std::runtime_error &)
        {
        }
        Check(first.next == &last && !last.next && !forwarding, "throwing downstream listener still restores links");
        forwarding = true;
        RouteInputBatch(
            &first, [](const Event &) { return true; }, [](Event *) {}, [](Event *) {}, forwarding);
        Check(forwarding, "nested forwarding preserves an outer guard");
    }
    std::cout << "Horde input routing: " << assertions << " assertions passed\n";
}
