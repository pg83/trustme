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
    // A backstop against runaway recursion, not a limit on legitimate nesting:
    // the compiler runs on a stack far larger than the default (see `main`), and
    // generated code can nest thousands of levels deep.
    #define MAX_INDENT_LEVEL 40000
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
        auto _tf_ = makeTraceLogRet(DEBUG_ENABLED ? __func__ : nullptr,                    \
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
    const char* tag_;

public:
    template<typename Info>
    TraceLog(const char* tag, Info&& infoCb)
        : tag_(tag)
    {
        if (tag_) {
            auto& os = debugOutput(gDebugIndentLevel, tag_);
            os << ">> (";
            infoCb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    TraceLog(const char* tag);

    ~TraceLog();
};

template<typename Ret>
class TraceLogRet {
    const char* tag_;
    Ret ret;

public:
    template<typename Info>
    TraceLogRet(const char* tag, Info&& infoCb, Ret&& ret)
        : tag_(tag)
        , ret(::std::forward<Ret>(ret))
    {
        if (tag_) {
            auto& os = debugOutput(gDebugIndentLevel, tag_);
            os << ">> (";
            infoCb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    ~TraceLogRet() {
        if (tag_) {
            UNINDENT();
            auto& os = debugOutput(gDebugIndentLevel, tag_);
            os << "<< (";
            ret(os);
            os << ")" << ::std::endl;
        }
    }
};

template<typename Info, typename Ret>
auto makeTraceLogRet(const char* tag, Info&& infoCb, Ret&& ret) {
    return TraceLogRet<::std::decay_t<Ret>>(
        tag,
        ::std::forward<Info>(infoCb),
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
