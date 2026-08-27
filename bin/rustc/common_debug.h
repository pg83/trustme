#pragma once

/*
 * Generic debug interface
 */

#include <vector>
#include <sstream>
#include <set>
#include <mutex>

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

class CommonDebugContext {
    int indentLevel_ = 0;
    const char* phase_ = "";
    bool phaseEnabled_ = false;
    bool enableHeaders_ = false;
    ::std::set<::std::string> disabledPhases_;
    ::std::mutex lock_;

public:
    void setPhase(const char* phaseName);
    void processEnable(const char* enableString);
    void disablePhase(const char* phaseName);
    void enablePhase(const char* phaseName);
    bool isEnabled() const;
    void enterScope(const char* name, DebugStreamCallback& cb);
    void leaveScope(const char* name);
    void print(DebugStreamCallback& cb);
};

template <typename F>
void DebugEnterScope(CommonDebugContext& context, const char* name, F f) {
    DebugStreamCb<F> cb(f);
    context.enterScope(name, cb);
}

template <typename F>
void DebugPrint(CommonDebugContext& context, F f) {
    DebugStreamCb<F> cb(f);
    context.print(cb);
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
            DebugPrint(debugContext, [&](auto& os) { \
                os << _DEBUG_fcn << ": " << fmt;   \
            });                                    \
        } while (0)
    #define TRACE_FUNCTION_F(fmt)               \
        DebugFunctionScope traceFunctionHdr { \
            debugContext, __FUNCTION__, [&](auto& os) { \
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
    CommonDebugContext& context;
    const char* name;

    template <typename F>
    DebugFunctionScope(CommonDebugContext& context, const char* name, F f)
        : context(context)
        , name(name)
    {
        DebugEnterScope(context, name, f);
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
