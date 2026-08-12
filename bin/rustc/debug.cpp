#include "debug.h"
#include <cstdint>
#include "debug_inner.h"
#include <set>
#include <iostream>
#include <iomanip>
#include "common.h" // FmtEscaped
#include <cstring>    // strchr

// TODO: Inline debug filter/caching
// - Cache messages for the current phase, clearing the cache (dropping) when various signatures match
//  > Similar to the `log_get_last_function.py` script

int g_debug_indent_level = 0;
bool g_debug_enabled = true;
::std::string g_cur_phase;
::std::set<::std::string> g_debug_disable_map;

bool debugEnabledUpdate() {
    if (g_debug_disable_map.count(g_cur_phase) != 0) {
        return false;
    } else {
        return true;
    }
}

::std::ostream& debugOutput(int indent, const char* function) {
    return ::std::cout << g_cur_phase << "- " << RepeatLitStr{" ", indent} << function << ": ";
}

DebugTimedPhase::DebugTimedPhase(const char* name)
    : mName(name)
{
    ::std::cout << mName << ": V V V" << ::std::endl;
    g_cur_phase = mName;
    g_debug_enabled = debugEnabledUpdate();
    start = clock();
}

DebugTimedPhase::~DebugTimedPhase() {
    auto end = clock();
    g_cur_phase = "";
    g_debug_enabled = debugEnabledUpdate();

    // TODO: Show wall time too?
    ::std::cout << "(" << ::std::fixed << ::std::setprecision(2) << static_cast<double>(end - start) / static_cast<double>(CLOCKS_PER_SEC) << " s) ";
    ::std::cout << mName << ": DONE";
    ::std::cout << ::std::endl;
}

extern void debugInitPhases(const char* envVarName, std::initializer_list<const char*> il) {
    for (const char* e : il) {
        g_debug_disable_map.insert(e);
    }

    // Mutate this map using an environment variable
    const char* debug_string = ::std::getenv(envVarName);
    if (debug_string) {
        while (debug_string[0]) {
            if (strcmp(debug_string, "*") == 0) {
                g_debug_disable_map.clear();
                return;
            }

            const char* end = strchr(debug_string, ':');

            ::std::string s;
            if (end) {
                s = ::std::string{debug_string, end};
                debug_string = end + 1;
            } else {
                s = debug_string;
            }
            if (g_debug_disable_map.erase(s) == 0) {
                ::std::cerr << "WARN: Unknown compiler phase '" << s << "' in $" << envVarName << ::std::endl;
            }
            if (!end) {
                break;
            }
        }
    }
}

::std::ostream& operator<<(::std::ostream& os, const FmtEscaped& x) {
    os << ::std::hex;
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
                uint8_t v = *s;
                if (v < 0x80) {
                    if (v < ' ' || v > 0x7F) {
                        os << "\\u{" << ::std::hex << (unsigned int)v << "}";
                    } else {
                        os << v;
                    }
                } else if (v < 0xC0)
                    ;
                else if (v < 0xE0) {
                    uint32_t val = (uint32_t)(v & 0x1F) << 6;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 0;
                    os << "\\u{" << ::std::hex << val << "}";
                } else if (v < 0xF0) {
                    uint32_t val = (uint32_t)(v & 0x0F) << 12;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 6;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 0;
                    os << "\\u{" << ::std::hex << val << "}";
                } else if (v < 0xF8) {
                    uint32_t val = (uint32_t)(v & 0x07) << 18;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 12;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 6;
                    v = (uint8_t)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (uint32_t)(v & 0x3F) << 0;
                    os << "\\u{" << ::std::hex << val << "}";
                }
                break;
        }
    }
    os << ::std::dec;
    return os;
}

NullSink::NullSink() {
}
TraceLog::TraceLog(const char* tag)
    : mTag(tag) {
    if (mTag) {
        auto& os = debugOutput(g_debug_indent_level, mTag);
        os << ">>" << ::std::endl;
        INDENT();
    }
}
TraceLog::~TraceLog() {
    if (mTag) {
        UNINDENT();
        auto& os = debugOutput(g_debug_indent_level, mTag);
        os << "<< ()" << ::std::endl;
    }
}
FmtLambda::FmtLambda(::std::function<void(::std::ostream&)> cb)
    : cb(cb) {
}

::std::ostream& operator<<(::std::ostream& os, const RepeatLitStr& r) {
    for (int i = 0; i < r.n; i++) {
        os << r.s;
    }
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const FmtLambda& x) {
    x.cb(os);
    return os;
}
