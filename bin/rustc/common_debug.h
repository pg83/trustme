#pragma once

/*
 * Generic debug interface
 */

#include <vector>
#include <sstream>

struct DebugStreamCallback {
    virtual void write(::std::ostream& os) = 0;
};

template <typename F>
struct DebugStreamCb final: DebugStreamCallback {
    F f;

    explicit DebugStreamCb(F f)
        : f(f)
    {
    }

    void write(::std::ostream& os) override {
        f(os);
    }
};

extern void DebugSetPhase(const char* phaseName);
extern void DebugProcessEnable(const char* enableString);
extern void DebugDisablePhase(const char* phaseName);
extern void DebugEnablePhase(const char* phaseName);
extern bool DebugIsEnabled();
extern void DebugEnterScopeCb(const char* name, DebugStreamCallback& cb);
extern void DebugLeaveScope(const char* name);
extern void DebugPrintCb(DebugStreamCallback& cb);

template <typename F>
void DebugEnterScope(const char* name, F f) {
    DebugStreamCb<F> cb(f);
    DebugEnterScopeCb(name, cb);
}

template <typename F>
void DebugPrint(F f) {
    DebugStreamCb<F> cb(f);
    DebugPrintCb(cb);
}

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
        DebugFunctionScope traceFunctionHdr { \
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
    static inline void formatToStream(::std::ostream& os) {
    }

    template <typename T, typename... A>
    static inline void formatToStream(::std::ostream& os, const T& v, const A&... a) {
        os << v;
        formatToStream(os, a...);
    }
}

struct DebugFunctionScope {
    const char* name;

    template <typename F>
    DebugFunctionScope(const char* name, F f)
        : name(name)
    {
        DebugEnterScope(name, f);
    }

    ~DebugFunctionScope();
};

template <typename... T>
::std::string format(const T&... v) {
    ::std::stringstream ss;
    formatToStream(ss, v...);
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
