#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Horde
{
    inline constexpr std::uint32_t kMaxCosavePayload = 16 * 1024 * 1024;

    template <class Reader>
    std::optional<std::string> ReadCosavePayload(Reader& reader, std::uint32_t length)
    {
        if (length < sizeof(std::uint32_t) || length - sizeof(std::uint32_t) > kMaxCosavePayload) {
            return std::nullopt;
        }
        std::uint32_t size = 0;
        if (reader.ReadRecordData(size) != sizeof(size) || size != length - sizeof(size)) {
            return std::nullopt;
        }
        std::string payload(size, '\0');
        if (reader.ReadRecordData(payload.data(), size) != size) return std::nullopt;
        return payload;
    }
}
