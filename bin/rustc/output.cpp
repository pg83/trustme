#include "output.h"

#include <std/lib/vector.h>

#include <string>
#include <vector>

using namespace stl;

FormattedHex formatHex(u64 value, unsigned width, bool uppercase) {
    return {value, width, uppercase};
}

template <>
void stl::output<ZeroCopyOutput, FormattedHex>(ZeroCopyOutput& out, FormattedHex value) {
    char buf[16];
    char* begin = buf + sizeof(buf);
    auto number = value.value;
    do {
        auto digit = static_cast<unsigned>(number & 0xF);
        *--begin = static_cast<char>(digit < 10 ? '0' + digit : (value.uppercase ? 'A' : 'a') + digit - 10);
        number >>= 4;
    } while (number);
    auto length = static_cast<unsigned>(buf + sizeof(buf) - begin);
    for (auto i = length; i < value.width; ++i) {
        out << '0';
    }
    out.write(begin, length);
}

template <>
void stl::output<ZeroCopyOutput, bool>(ZeroCopyOutput& out, bool value) {
    out << StringView(value ? u8"1" : u8"0");
}

template <>
void stl::output<ZeroCopyOutput, char>(ZeroCopyOutput& out, char value) {
    out.write(&value, 1);
}

template <>
void stl::output<ZeroCopyOutput, char8_t>(ZeroCopyOutput& out, char8_t value) {
    out.write(&value, 1);
}

template <>
void stl::output<ZeroCopyOutput, std::string>(ZeroCopyOutput& out, const std::string& value) {
    out.write(value.data(), value.size());
}

template <>
void stl::output<ZeroCopyOutput, std::string_view>(ZeroCopyOutput& out, std::string_view value) {
    out.write(value.data(), value.size());
}

template <>
void stl::output<ZeroCopyOutput, char*>(ZeroCopyOutput& out, char* value) {
    out << StringView(value);
}

template <>
void stl::output<ZeroCopyOutput, const void*>(ZeroCopyOutput& out, const void* value) {
    out << StringView("0x") << formatHex(reinterpret_cast<uintptr_t>(value));
}

template <>
void stl::output<ZeroCopyOutput, Vector<char8_t>>(ZeroCopyOutput& out, const Vector<char8_t>& values) {
    outCont(out, values);
}
