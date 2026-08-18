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

int gDebugIndentLevel = 0;
bool gDebugEnabled = true;
::std::string gCurPhase;
::std::set<::std::string> gDebugDisableMap;

bool debugEnabledUpdate() {
    if (gDebugDisableMap.count(gCurPhase) != 0) {
        return false;
    } else {
        return true;
    }
}

::std::ostream& debugOutput(int indent, const char* function) {
    return ::std::cout << gCurPhase << "- " << RepeatLitStr{" ", indent} << function << ": ";
}

DebugTimedPhase::DebugTimedPhase(const char* name)
    : name_(name)
{
    ::std::cout << name_ << ": V V V" << ::std::endl;
    gCurPhase = name_;
    gDebugEnabled = debugEnabledUpdate();
    start = clock();
}

DebugTimedPhase::~DebugTimedPhase() {
    auto end = clock();
    gCurPhase = "";
    gDebugEnabled = debugEnabledUpdate();

    // TODO: Show wall time too?
    ::std::cout << "(" << ::std::fixed << ::std::setprecision(2) << static_cast<double>(end - start) / static_cast<double>(CLOCKS_PER_SEC) << " s) ";
    ::std::cout << name_ << ": DONE";
    ::std::cout << ::std::endl;
}

extern void debugInitPhases(const char* envVarName, std::initializer_list<const char*> il) {
    for (const char* e : il) {
        gDebugDisableMap.insert(e);
    }

    // Mutate this map using an environment variable
    const char* debugString = ::std::getenv(envVarName);
    if (debugString) {
        while (debugString[0]) {
            if (strcmp(debugString, "*") == 0) {
                gDebugDisableMap.clear();
                return;
            }

            const char* end = strchr(debugString, ':');

            ::std::string s;
            if (end) {
                s = ::std::string{debugString, end};
                debugString = end + 1;
            } else {
                s = debugString;
            }
            if (gDebugDisableMap.erase(s) == 0) {
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
                u8 v = *s;
                if (v < 0x80) {
                    if (v < ' ' || v > 0x7F) {
                        os << "\\u{" << ::std::hex << (unsigned int)v << "}";
                    } else {
                        os << static_cast<char>(v);
                    }
                } else if (v < 0xC0)
                    ;
                else if (v < 0xE0) {
                    u32 val = (u32)(v & 0x1F) << 6;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
                    os << "\\u{" << ::std::hex << val << "}";
                } else if (v < 0xF0) {
                    u32 val = (u32)(v & 0x0F) << 12;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 6;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
                    os << "\\u{" << ::std::hex << val << "}";
                } else if (v < 0xF8) {
                    u32 val = (u32)(v & 0x07) << 18;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 12;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 6;
                    v = (u8)*++s;
                    if ((v & 0xC0) != 0x80) {
                        s--;
                        continue;
                    }
                    val |= (u32)(v & 0x3F) << 0;
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
    : tag_(tag) {
    if (tag_) {
        auto& os = debugOutput(gDebugIndentLevel, tag_);
        os << ">>" << ::std::endl;
        INDENT();
    }
}
TraceLog::~TraceLog() {
    if (tag_) {
        UNINDENT();
        auto& os = debugOutput(gDebugIndentLevel, tag_);
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
