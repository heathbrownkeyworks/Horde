#pragma once
#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Horde::ImGuiUI::input
{
enum class Device
{
    Keyboard,
    Mouse,
    Gamepad
};
enum class Phase
{
    Down,
    Held,
    Up
};

// A button pressed inside Horde stays hidden through its release, even if
// closing the menu happens first. A previously visible press still gets its
// release, so another listener cannot be left holding a modifier forever.
class InputOwnership
{
  public:
    static constexpr std::uint32_t PrintScreen = 0xB7;

    bool HideButton(bool owned, Device device, std::uint32_t code, Phase phase)
    {
        auto &hidden = _hidden[static_cast<std::size_t>(device)];
        if (phase == Phase::Down)
        {
            hidden.erase(code);
            if (owned && !(device == Device::Keyboard && code == PrintScreen))
                hidden.insert(code);
        }
        const bool hide = hidden.contains(code);
        if (phase == Phase::Up)
            hidden.erase(code);
        // Do not let a pre-held key start repeating hotkeys inside the panel.
        return hide || (owned && phase == Phase::Held && !(device == Device::Keyboard && code == PrintScreen));
    }

    void Disconnect(Device device)
    {
        _hidden[static_cast<std::size_t>(device)].clear();
    }
    static bool HideCharacter(bool owned)
    {
        return owned;
    }
    // Neutral samples must remain visible to clear other listeners' cached axes.
    static bool HideStick(bool owned, float x, float y)
    {
        return owned && (x != 0 || y != 0);
    }

  private:
    std::array<std::unordered_set<std::uint32_t>, 3> _hidden;
};

// The owner sees the original batch once. Downstream hooks/sinks see a filtered
// chain, and the engine gets its original links back even if forwarding throws.
// This is also used by the tests with lightweight synthetic events.
template <class Event, class Hide, class Owner, class Forward>
void RouteInputBatch(Event *head, Hide &&hide, Owner &&owner, Forward &&forward, bool &forwarding)
{
    struct Link
    {
        Event *event;
        Event *next;
        bool hidden;
    };
    std::vector<Link> links;
    bool anyHidden = false;
    for (auto *event = head; event; event = event->next)
    {
        const bool hidden = hide(*event);
        links.push_back({event, event->next, hidden});
        anyHidden |= hidden;
    }
    if (!anyHidden)
    {
        forward(head);
        return;
    }
    owner(head);
    struct Restore
    {
        std::vector<Link> &links;
        bool &forwarding;
        bool previous;
        ~Restore()
        {
            for (const auto &link : links)
                link.event->next = link.next;
            forwarding = previous;
        }
    } restore{links, forwarding, forwarding};
    Event *visible = nullptr;
    Event *tail = nullptr;
    for (const auto &link : links)
        if (!link.hidden)
        {
            (tail ? tail->next : visible) = link.event;
            tail = link.event;
        }
    if (tail)
        tail->next = nullptr;
    forwarding = true;
    forward(visible);
}
} // namespace Horde::ImGuiUI::input
