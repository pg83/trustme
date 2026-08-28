#include "common.h"

FmtEscaped::FmtEscaped(const std::string& s)
    : s(s.c_str())
    , e(s.c_str() + s.size())
{
}

std::ostream& operator<<(std::ostream& os, const FmtEscaped& x) {
    os << std::hex;
    for (auto s = x.s; s != x.e; s++) {
        switch (*s) {
            case '\0':
                os << "\\0";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '"':
                os << "\\\"";
                break;
            default:
                u8 v = *s;
                if (v < 0x80) {
                    if (v < ' ' || v > 0x7F) {
                        os << "\\u{" << std::hex << (unsigned int)v << "}";
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
                    os << "\\u{" << std::hex << val << "}";
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
                    os << "\\u{" << std::hex << val << "}";
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
                    os << "\\u{" << std::hex << val << "}";
                }
                break;
        }
    }
    os << std::dec;
    return os;
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
