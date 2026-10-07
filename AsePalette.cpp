#include "pch.h"
#include "AsePalette.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ase {

namespace {

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : m_data(data) {
    }

    size_t Remaining() const {
        return m_data.size() - m_pos;
    }

    uint8_t Byte() {
        if (!Remaining()) {
            throw ParseError{L"The ASE file is truncated."};
        }

        return std::to_integer<uint8_t>(m_data[m_pos++]);
    }

    uint16_t U16() {
        const auto hi = Byte();
        return uint16_t((hi << 8) | Byte());
    }

    uint32_t U32() {
        const auto hi = U16();
        return (uint32_t(hi) << 16) | U16();
    }

    float Float() {
        const auto value = std::bit_cast<float>(U32());

        if (!std::isfinite(value)) {
            throw ParseError{L"The ASE file contains an invalid numeric value."};
        }

        return value;
    }

    Reader Block(size_t length) {
        if (length > Remaining()) {
            throw ParseError{L"An ASE block extends beyond the end of the file."};
        }

        Reader result(m_data.subspan(m_pos, length));
        m_pos += length;

        return result;
    }

    std::wstring Name() {
        const auto count = U16();

        if (count == 0 || size_t(count) * 2 > Remaining()) {
            throw ParseError{L"Invalid name length in the ASE file."};
        }

        std::wstring result;
        result.reserve(count - 1);
        bool needsLow = false;

        for (unsigned i = 0; i < count; ++i) {
            const auto c = U16();

            if (i + 1 == unsigned(count)) {
                if (c != 0 || needsLow) {
                    throw ParseError{L"Invalid UTF-16 string in the ASE file."};
                }

                break;
            }

            if (c == 0 || (needsLow && (c < 0xdc00 || c > 0xdfff)) || (!needsLow && c >= 0xdc00 && c <= 0xdfff)) {
                throw ParseError{L"Invalid UTF-16 string in the ASE file."};
            }

            needsLow = c >= 0xd800 && c <= 0xdbff;
            result.push_back(wchar_t(c));
        }

        return result;
    }

private:
    std::span<const std::byte> m_data;
    size_t m_pos = 0;
};

uint8_t Channel(double value) {
    return uint8_t(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}

// Encode linear sRGB using its piecewise transfer function (IEC 61966-2-1).
// Definition: https://registry.color.org/rgb-registry/srgb
double Gamma(double value) {
    return value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

void Convert(Entry& entry) {
    const std::string model(entry.model.data(), 4);
    const auto& c = entry.components;
    double r = 0, g = 0, b = 0;

    if (model == "RGB ") {
        r = c[0];
        g = c[1];
        b = c[2];
    }
    else if (model == "Gray") {
        r = g = b = c[0];
    }
    else if (model == "CMYK") {
        r = (1 - c[0]) * (1 - c[3]);
        g = (1 - c[1]) * (1 - c[3]);
        b = (1 - c[2]) * (1 - c[3]);
        entry.approximate = true;
    }
    else if (model == "LAB ") {
        // ASE stores L*/100; a* and b* are unscaled. Lab uses the D50 reference white.
        const double fy = (100.0 * c[0] + 16) / 116;
        const auto inverse = [](double t) {
            constexpr double delta = 6.0 / 29;
            return t > delta ? t * t * t : 3 * delta * delta * (t - 4.0 / 29);
        };

        const double x50 = 0.96422 * inverse(fy + c[1] / 500.0);
        const double y50 = inverse(fy);
        const double z50 = 0.82521 * inverse(fy - c[2] / 200.0);

        // Rounded Bradford chromatic adaptation matrix: XYZ D50 -> XYZ D65.
        // Matching coefficients: https://www.w3.org/TR/2019/WD-css-color-4-20191105/#color-conversion-code
        const double x = 0.9555766 * x50 - 0.0230393 * y50 + 0.0631636 * z50;
        const double y = -0.0282895 * x50 + 1.0099416 * y50 + 0.0210077 * z50;
        const double z = 0.0122982 * x50 - 0.0204830 * y50 + 1.3299098 * z50;

        // XYZ D65 -> linear sRGB matrix, followed by the sRGB transfer function.
        // Matching coefficients: https://www.w3.org/TR/2019/WD-css-color-4-20191105/#color-conversion-code
        r = Gamma(3.2404542 * x - 1.5371385 * y - 0.4985314 * z);
        g = Gamma(-0.9692660 * x + 1.8760108 * y + 0.0415560 * z);
        b = Gamma(0.0556434 * x - 0.2040259 * y + 1.0572252 * z);

        entry.approximate = true;
    }

    entry.rgb = {Channel(r), Channel(g), Channel(b)};
}

} // namespace

Palette Parse(std::span<const std::byte> bytes) {
    if (bytes.size() > MaxBytes) {
        throw ParseError{L"The ASE file exceeds the 64 MiB limit."};
    }

    // Validate the header before interpreting any block payloads.
    Reader file(bytes);
    if (file.U32() != 0x41534546) {
        throw ParseError{L"Not an Adobe ASE file (missing ASEF signature)."};
    }

    if (file.U32() != 0x00010000) {
        throw ParseError{L"Only ASE version 1.0 is supported."};
    }

    const auto count = file.U32();
    if (count > MaxBlocks) {
        throw ParseError{L"The ASE file contains more than 100,000 blocks."};
    }

    Palette result;
    unsigned depth = 0;

    for (uint32_t i = 0; i < count; ++i) {
        const auto type = file.U16();
        const auto length = file.U32();
        auto block = file.Block(length);
        Entry entry;

        if (type == 0xc001) {
            entry.kind = EntryKind::GroupStart;
            entry.name = block.Name();
            ++depth;
        }
        else if (type == 0xc002) {
            if (!depth) {
                throw ParseError{L"Invalid ASE group structure."};
            }

            entry.kind = EntryKind::GroupEnd;
            --depth;
        }
        else if (type == 1) {
            entry.name = block.Name();
            for (auto& c : entry.model) {
                c = char(block.Byte());
            }

            const std::string model(entry.model.data(), 4);

            if (model == "RGB " || model == "LAB ") {
                entry.componentCount = 3;
            }
            else if (model == "CMYK") {
                entry.componentCount = 4;
            }
            else if (model == "Gray") {
                entry.componentCount = 1;
            }

            if (entry.componentCount) {
                for (unsigned j = 0; j < entry.componentCount; ++j) {
                    entry.components[j] = block.Float();
                    if ((model != "LAB " || j == 0) && (entry.components[j] < 0 || entry.components[j] > 1)) {
                        throw ParseError{L"An ASE color component is outside the allowed range."};
                    }
                }

                entry.colorType = block.U16();
                if (entry.colorType > 2) {
                    throw ParseError{L"Invalid ASE color type."};
                }

                entry.supported = true;
                Convert(entry);
            }
            else {
                // Unknown model: preserve the name/tag; its component layout is not known.
                if (block.Remaining() < 2) {
                    throw ParseError{L"An ASE block with an unknown color model is truncated."};
                }

                (void)block.Block(block.Remaining() - 2);
                entry.colorType = block.U16();
                if (entry.colorType > 2) {
                    throw ParseError{L"Invalid ASE color type."};
                }

                ++result.colorCount;
                result.entries.push_back(std::move(entry));
                continue;
            }

            ++result.colorCount;
        }
        else {
            continue;
        }

        if (block.Remaining()) {
            throw ParseError{L"Invalid length for a known ASE block."};
        }

        result.entries.push_back(std::move(entry));
    }

    if (depth || file.Remaining()) {
        throw ParseError{L"Invalid ASE block structure or block count."};
    }

    return result;
}

std::wstring Describe(const Entry& entry) {
    std::wostringstream text;
    text.imbue(std::locale::classic());

    text << (entry.name.empty() ? L"Unnamed" : entry.name) << L"\n";
    for (char c : entry.model) {
        text << ((c >= 32 && c < 127) ? wchar_t(c) : L'?');
    }

    if (!entry.supported) {
        text << L"\nUnsupported color model";
        return text.str();
    }

    text << L": " << std::setprecision(6);
    for (unsigned i = 0; i < entry.componentCount; ++i) {
        if (i) {
            text << L", ";
        }
        text << entry.components[i];
    }

    const wchar_t* types[] = {L"Global", L"Spot", L"Normal"};
    text << L"\n" << types[entry.colorType];
    if (entry.approximate) {
        text << L"\nApproximate sRGB rendering without an ICC profile";
    }

    return text.str();
}

} // namespace ase
