#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ase {

inline constexpr size_t MaxBytes = 64 * 1024 * 1024;
inline constexpr uint32_t MaxBlocks = 100000;

enum class EntryKind {
    Color,
    GroupStart,
    GroupEnd
};

struct Entry {
    EntryKind kind = EntryKind::Color;
    std::wstring name;

    std::array<char, 4> model{};
    std::array<float, 4> components{};
    unsigned componentCount = 0;
    uint16_t colorType = 2;

    std::array<uint8_t, 3> rgb{};
    bool supported = false;
    bool approximate = false;
};

struct Palette {
    std::vector<Entry> entries;
    size_t colorCount = 0;
};

struct ParseError {
    const wchar_t* message;
};

Palette Parse(std::span<const std::byte> bytes);

std::wstring Describe(const Entry& entry);

} // namespace ase
