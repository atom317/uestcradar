#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cycore {
namespace du {
namespace common {

using StreamMetadataMap = std::unordered_map<std::string, std::string>;
using StreamMetadataPtr = std::shared_ptr<const StreamMetadataMap>;

inline StreamMetadataPtr MakeStreamMetadata(StreamMetadataMap metadata) {
    if (metadata.empty()) {
        return {};
    }
    return std::make_shared<const StreamMetadataMap>(std::move(metadata));
}

inline const std::string* FindMetadataValue(const StreamMetadataPtr& metadata,
                                            std::string_view key) {
    if (!metadata) {
        return nullptr;
    }
    const auto it = metadata->find(std::string(key));
    if (it == metadata->end()) {
        return nullptr;
    }
    return &it->second;
}

template <typename T>
bool TryParseMetadataValue(const std::string& value, T& out);

template <>
inline bool TryParseMetadataValue<std::string>(const std::string& value, std::string& out) {
    out = value;
    return true;
}

template <>
inline bool TryParseMetadataValue<std::uint32_t>(const std::string& value, std::uint32_t& out) {
    if (value.empty()) {
        return false;
    }
    try {
        std::size_t parsed_chars = 0;
        const auto parsed = std::stoull(value, &parsed_chars, 10);
        if (parsed_chars != value.size() ||
            parsed > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        out = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

template <typename T>
T GetMetadataOr(const StreamMetadataPtr& metadata, std::string_view key, T default_value) {
    const auto* value = FindMetadataValue(metadata, key);
    if (!value) {
        return default_value;
    }
    T parsed{};
    if (!TryParseMetadataValue<T>(*value, parsed)) {
        return default_value;
    }
    return parsed;
}

template <typename T>
T RequireMetadata(const StreamMetadataPtr& metadata, std::string_view key) {
    const auto* value = FindMetadataValue(metadata, key);
    if (!value) {
        throw std::invalid_argument("Missing stream metadata key: " + std::string(key));
    }
    T parsed{};
    if (!TryParseMetadataValue<T>(*value, parsed)) {
        throw std::invalid_argument("Invalid stream metadata value for key: " + std::string(key));
    }
    return parsed;
}

} // namespace common
} // namespace du
} // namespace cycore
