#include "common.h"

#include "output.h"

#include <std/lib/vector.h>

#include <map>
#include <set>

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, std::vector<std::string>>(ZeroCopyOutput& out, const std::vector<std::string>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, Vector<size_t>>(ZeroCopyOutput& out, const Vector<size_t>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::pair<size_t, size_t>>(ZeroCopyOutput& out, const std::pair<size_t, size_t>& value) {
    out << StringView("(") << value.first << StringView(", ") << value.second << StringView(")");
}

template <>
void stl::output<ZeroCopyOutput, Vector<unsigned>>(ZeroCopyOutput& out, const Vector<unsigned>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<std::vector<std::string>>>(ZeroCopyOutput& out, const std::vector<std::vector<std::string>>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::pair<const unsigned, unsigned>>(ZeroCopyOutput& out, std::pair<const unsigned, unsigned> value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void stl::output<ZeroCopyOutput, std::map<unsigned, unsigned>>(ZeroCopyOutput& out, const std::map<unsigned, unsigned>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::set<unsigned>>(ZeroCopyOutput& out, const std::set<unsigned>& values) {
    outCont(out, values);
}

FmtEscaped::FmtEscaped(const std::string& s)
    : s(s.c_str())
    , e(s.c_str() + s.size())
{
}

template <>
void stl::output<ZeroCopyOutput, FmtEscaped>(ZeroCopyOutput& os, FmtEscaped x) {
    bool afterHexEscape = false;
    const auto emitByte = [&](u8 byte) {
        os << StringView("\\x") << formatHex(byte, 2, true);
        afterHexEscape = true;
    };
    for (auto s = x.begin(); s != x.end(); s++) {
        const auto lead = static_cast<u8>(*s);
        if (afterHexEscape && (('0' <= lead && lead <= '9') || ('a' <= lead && lead <= 'f') || ('A' <= lead && lead <= 'F'))) {
            emitByte(lead);
            continue;
        }
        afterHexEscape = false;
        switch (lead) {
            case '\0':
                if (s + 1 != x.end() && '0' <= s[1] && s[1] <= '7') {
                    emitByte(lead);
                } else {
                    os << StringView("\\0");
                }
                continue;
            case '\n':
                os << StringView("\\n");
                continue;
            case '\\':
                os << StringView("\\\\");
                continue;
            case '"':
                os << StringView("\\\"");
                continue;
            default:
                break;
        }
        if (lead < 0x80) {
            if (lead < ' ' || lead == 0x7F) {
                os << StringView("\\u{") << formatHex(lead) << StringView("}");
            } else {
                os << static_cast<char>(lead);
            }
            continue;
        }
        const size_t width = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (width == 0 || static_cast<size_t>(x.end() - s) < width) {
            emitByte(lead);
            continue;
        }
        u32 value = lead & (0x7F >> width);
        bool valid = true;
        for (size_t i = 1; i < width; i++) {
            const auto byte = static_cast<u8>(s[i]);
            if ((byte & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            value = (value << 6) | (byte & 0x3F);
        }
        const u32 minimum = width == 2 ? 0x80 : width == 3 ? 0x800 : 0x10000;
        if (!valid || value < minimum || value > 0x10FFFF || (0xD800 <= value && value <= 0xDFFF)) {
            emitByte(lead);
            continue;
        }
        os << StringView("\\u{") << formatHex(value) << StringView("}");
        s += width - 1;
    }
}

template <>
void stl::output<ZeroCopyOutput, RepeatLitStr>(ZeroCopyOutput& os, RepeatLitStr value) {
    for (int i = 0; i < value.n; ++i) {
        os << value.s;
    }
}
