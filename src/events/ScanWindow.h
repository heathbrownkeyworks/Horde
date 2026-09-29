#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace Horde
{
    class ScanWindow
    {
    public:
        void Request(std::int64_t now, int duration, bool dialogue)
        {
            std::scoped_lock lock(_mutex);
            if (now >= _until) _dialogue = false;
            _dialogue = _dialogue || dialogue;
            _until = std::max(_until, now + duration);
        }

        struct State { bool active; bool dialogue; };
        State Read(std::int64_t now) const
        {
            std::scoped_lock lock(_mutex);
            return {now < _until, now < _until && _dialogue};
        }

        void Reset()
        {
            std::scoped_lock lock(_mutex);
            _until = 0;
            _dialogue = false;
        }

    private:
        mutable std::mutex _mutex;
        std::int64_t _until = 0;
        bool _dialogue = false;
    };
}
