#include "common.h"

#include "output.h"

#include <std/lib/vector.h>

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, Ordering>(ZeroCopyOutput& out, Ordering value) {
    out << static_cast<int>(value);
}

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

Ordering ord(bool l, bool r) {
    if (l == r) {
        return OrdEqual;
    } else if (l) {
        return OrdGreater;
    } else {
        return OrdLess;
    }
}

Ordering ord(const std::string& l, const std::string& r) {
    if (l == r) {
        return OrdEqual;
    } else if (l > r) {
        return OrdGreater;
    } else {
        return OrdLess;
    }
}

template <>
void stl::output<ZeroCopyOutput, FmtEscaped>(ZeroCopyOutput& os, FmtEscaped x) {
    for (auto s = x.begin(); s != x.end(); s++) {
        switch (*s) {
            case '\0':
                os << StringView("\\0");
                break;
            case '\n':
                os << StringView("\\n");
                break;
            case '\\':
                os << StringView("\\\\");
                break;
            case '"':
                os << StringView("\\\"");
                break;
            default:
                u8 v = *s;
                if (v < 0x80) {
                    if (v < ' ' || v > 0x7F) {
                        os << StringView("\\u{");
                        os << formatHex(v);
                        os << StringView("}");
                    } else {
                        os << static_cast<char>(v);
                    }
                } else if (v < 0xC0)
                    ;
                else if (v < 0xE0) {
                    u32 val = (u32)(v & 0x1F) << 6;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
                    os << StringView("\\u{");
                    os << formatHex(val);
                    os << StringView("}");
                } else if (v < 0xF0) {
                    u32 val = (u32)(v & 0x0F) << 12;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 6;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
                    os << StringView("\\u{");
                    os << formatHex(val);
                    os << StringView("}");
                } else if (v < 0xF8) {
                    u32 val = (u32)(v & 0x07) << 18;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 12;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 6;
                    v = (u8) * ++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
                    os << StringView("\\u{");
                    os << formatHex(val);
                    os << StringView("}");
                }
                break;
        }
    }
}

template <>
void stl::output<ZeroCopyOutput, RepeatLitStr>(ZeroCopyOutput& os, RepeatLitStr value) {
    for (int i = 0; i < value.n; ++i) {
        os << value.s;
    }
}
