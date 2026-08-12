#pragma once

/*
 * mrustc common tools
 * - by John Hodge (Mutabah)
 *
 * tools/common/debug.h
 * - Generic debug interface (used by minicargo/standalone_miri)
 */

#include <functional>
#include <vector>
#include <sstream>

typedef ::std::function<void(::std::ostream& os)> dbg_cb_t;
extern void DebugSetPhase(const char* phase_name);
extern void DebugProcessEnable(const char* enable_string);
extern void DebugDisablePhase(const char* phase_name);
extern void DebugEnablePhase(const char* phase_name);
extern bool DebugIsEnabled();
extern void DebugEnterScope(const char* name, dbg_cb_t);
extern void DebugLeaveScope(const char* name, dbg_cb_t);
extern void DebugPrint(dbg_cb_t cb);

#if defined(NOLOG)
    #define DEBUG(fmt) \
        do {           \
        } while (0)
    #define TRACE_FUNCTION_F(fmt) \
        do {                      \
        } while (0)
#else
    #define DEBUG(fmt)                             \
        do {                                       \
            const char* _DEBUG_fcn = __FUNCTION__; \
            DebugPrint([&](auto& os) {            \
                os << _DEBUG_fcn << ": " << fmt;   \
            });                                    \
        } while (0)
    #define TRACE_FUNCTION_F(fmt)               \
        DebugFunctionScope trace_function_hdr { \
            __FUNCTION__, [&](auto& os) {       \
                os << fmt;                      \
            }                                   \
        }
#endif
#define TODO(fmt)                                      \
    do {                                               \
        ::std::cerr << "TODO: " << fmt << ::std::endl; \
        abort();                                       \
    } while (0)

template <typename T>
::std::ostream& operator<<(::std::ostream& os, const ::std::vector<T>& v);

namespace {
    static inline void format_to_stream(::std::ostream& os) {
    }

    template <typename T, typename... A>
    static inline void format_to_stream(::std::ostream& os, const T& v, const A&... a) {
        os << v;
        format_to_stream(os, a...);
    }
}

struct DebugFunctionScope {
    const char* m_name;

    DebugFunctionScope(const char* name, dbg_cb_t cb);

    ~DebugFunctionScope();
};

template <typename... T>
::std::string format(const T&... v) {
    ::std::stringstream ss;
    format_to_stream(ss, v...);
    return ss.str();
}

template <typename T>
::std::ostream& operator<<(::std::ostream& os, const ::std::vector<T>& v) {
    bool first = true;
    for (const auto& e : v) {
        if (!first) {
            os << ",";
        }
        os << e;
        first = false;
    }
    return os;
}
