#pragma once

#include <sstream>
#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

extern int gDebugIndentLevel;
extern bool gDebugEnabled;

#ifndef DEBUG_EXTRA_ENABLE
    #define DEBUG_EXTRA_ENABLE // Files can override this with their own flag if needed (e.g. `&& g_my_debug_on`)
#endif

#ifndef DISABLE_DEBUG
    // Note: Set for `curl-sys-0_4_82` build script, which has a log chain of method calls
    #define MAX_INDENT_LEVEL 450
    #define INDENT()                                         \
        do {                                                 \
            gDebugIndentLevel += 1;                       \
            assert(gDebugIndentLevel < MAX_INDENT_LEVEL); \
        } while (0)
    #define UNINDENT()                 \
        do {                           \
            gDebugIndentLevel -= 1; \
        } while (0)
    #define DEBUG_ENABLED (debugEnabled() DEBUG_EXTRA_ENABLE)
    #define DEBUG(ss)                                                                              \
        do {                                                                                       \
            if (DEBUG_ENABLED) {                                                                   \
                debugOutput(gDebugIndentLevel, __FUNCTION__) << ss << std::dec << ::std::endl; \
            }                                                                                      \
        } while (0)
    #define TRACE_FUNCTION TraceLog _tf_(DEBUG_ENABLED ? __func__ : nullptr)
    #define TRACE_FUNCTION_F(ss)                                                      \
        TraceLog _tf_(DEBUG_ENABLED ? __func__ : nullptr, [&](::std::ostream& __os) { \
            __os << ss;                                                               \
        })
    #define TRACE_FUNCTION_FR(ss, ss2)                                                        \
        auto _tf_ = make_trace_log_ret(DEBUG_ENABLED ? __func__ : nullptr,                    \
            [&](::std::ostream& __os) { __os << ss; },                                        \
            [&](::std::ostream& __os) { __os << ss2; })
#else
    #define INDENT() \
        do {         \
        } while (0)
    #define UNINDENT() \
        do {           \
        } while (0)
    #define DEBUG(ss)                       \
        do {                                \
            (void)sizeof(::NullSink() << ss); \
        } while (0)
    #define TRACE_FUNCTION \
        do {               \
        } while (0)
    #define TRACE_FUNCTION_F(ss)            \
        do {                                \
            (void)sizeof(::NullSink() << ss); \
        } while (0)
    #define TRACE_FUNCTION_FR(ss, ss2)       \
        do {                                 \
            (void)sizeof(::NullSink() << ss);  \
            (void)sizeof(::NullSink() << ss2); \
        } while (0)
#endif

inline bool debugEnabled() {
    return gDebugEnabled;
}
extern ::std::ostream& debugOutput(int indent, const char* function);

struct RepeatLitStr {
    const char* s;
    int n;

    friend ::std::ostream& operator<<(::std::ostream& os, const RepeatLitStr& r);
};

class NullSink {
public:
    NullSink();

    template <typename T>
    const NullSink& operator<<(const T&) const {
        return *this;
    }
};

class TraceLog {
    const char* mTag;

public:
    template<typename Info>
    TraceLog(const char* tag, Info&& info_cb)
        : mTag(tag)
    {
        if (mTag) {
            auto& os = debugOutput(gDebugIndentLevel, mTag);
            os << ">> (";
            info_cb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    TraceLog(const char* tag);

    ~TraceLog();
};

template<typename Ret>
class TraceLogRet {
    const char* mTag;
    Ret ret;

public:
    template<typename Info>
    TraceLogRet(const char* tag, Info&& info_cb, Ret&& ret)
        : mTag(tag)
        , ret(::std::forward<Ret>(ret))
    {
        if (mTag) {
            auto& os = debugOutput(gDebugIndentLevel, mTag);
            os << ">> (";
            info_cb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    ~TraceLogRet() {
        if (mTag) {
            UNINDENT();
            auto& os = debugOutput(gDebugIndentLevel, mTag);
            os << "<< (";
            ret(os);
            os << ")" << ::std::endl;
        }
    }
};

template<typename Info, typename Ret>
auto make_trace_log_ret(const char* tag, Info&& info_cb, Ret&& ret) {
    return TraceLogRet<::std::decay_t<Ret>>(
        tag,
        ::std::forward<Info>(info_cb),
        ::std::forward<Ret>(ret)
    );
}

struct FmtLambda {
    ::std::function<void(::std::ostream&)> cb;

    FmtLambda(::std::function<void(::std::ostream&)> cb);

    friend ::std::ostream& operator<<(::std::ostream& os, const FmtLambda& x);
};

#define FMT_CB(os, ...)         \
    ::FmtLambda([&](auto& os) { \
        __VA_ARGS__;            \
    })
#define FMT_CB_S(...)            \
    ::FmtLambda([&](auto& _os) { \
        _os << __VA_ARGS__;      \
    })
