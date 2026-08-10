#pragma once
#include <sstream>
#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

extern int g_debug_indent_level;
extern bool g_debug_enabled;

#ifndef DEBUG_EXTRA_ENABLE
    #define DEBUG_EXTRA_ENABLE // Files can override this with their own flag if needed (e.g. `&& g_my_debug_on`)
#endif

#ifndef DISABLE_DEBUG
    // Note: Set for `curl-sys-0_4_82` build script, which has a log chain of method calls
    #define MAX_INDENT_LEVEL 450
    #define INDENT()                                         \
        do {                                                 \
            g_debug_indent_level += 1;                       \
            assert(g_debug_indent_level < MAX_INDENT_LEVEL); \
        } while (0)
    #define UNINDENT()                 \
        do {                           \
            g_debug_indent_level -= 1; \
        } while (0)
    #define DEBUG_ENABLED (debug_enabled() DEBUG_EXTRA_ENABLE)
    #define DEBUG(ss)                                                                              \
        do {                                                                                       \
            if (DEBUG_ENABLED) {                                                                   \
                debug_output(g_debug_indent_level, __FUNCTION__) << ss << std::dec << ::std::endl; \
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
            if (false)                      \
                (void)(::NullSink() << ss); \
        } while (0)
    #define TRACE_FUNCTION \
        do {               \
        } while (0)
    #define TRACE_FUNCTION_F(ss)            \
        do {                                \
            if (false)                      \
                (void)(::NullSink() << ss); \
        } while (0)
    #define TRACE_FUNCTION_FR(ss, ss2)       \
        do {                                 \
            if (false)                       \
                (void)(::NullSink() << ss);  \
            if (false)                       \
                (void)(::NullSink() << ss2); \
        } while (0)
#endif

inline bool debug_enabled() {
    return g_debug_enabled;
}
extern ::std::ostream& debug_output(int indent, const char* function);

struct RepeatLitStr {
    const char* s;
    int n;

    friend ::std::ostream& operator<<(::std::ostream& os, const RepeatLitStr& r) {
        for (int i = 0; i < r.n; i++) {
            os << r.s;
        }
        return os;
    }
};

class NullSink {
public:
    NullSink() {
    }

    template <typename T>
    const NullSink& operator<<(const T&) const {
        return *this;
    }
};

class TraceLog {
    const char* m_tag;

public:
    template<typename Info>
    TraceLog(const char* tag, Info&& info_cb)
        : m_tag(tag)
    {
        if (m_tag) {
            auto& os = debug_output(g_debug_indent_level, m_tag);
            os << ">> (";
            info_cb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    TraceLog(const char* tag)
        : m_tag(tag)
    {
        if (m_tag) {
            auto& os = debug_output(g_debug_indent_level, m_tag);
            os << ">>" << ::std::endl;
            INDENT();
        }
    }

    ~TraceLog() {
        if (m_tag) {
            UNINDENT();
            auto& os = debug_output(g_debug_indent_level, m_tag);
            os << "<< ()" << ::std::endl;
        }
    }
};

template<typename Ret>
class TraceLogRet {
    const char* m_tag;
    Ret m_ret;

public:
    template<typename Info>
    TraceLogRet(const char* tag, Info&& info_cb, Ret&& ret)
        : m_tag(tag)
        , m_ret(::std::forward<Ret>(ret))
    {
        if (m_tag) {
            auto& os = debug_output(g_debug_indent_level, m_tag);
            os << ">> (";
            info_cb(os);
            os << ")" << ::std::endl;
            INDENT();
        }
    }

    ~TraceLogRet() {
        if (m_tag) {
            UNINDENT();
            auto& os = debug_output(g_debug_indent_level, m_tag);
            os << "<< (";
            m_ret(os);
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
    ::std::function<void(::std::ostream&)> m_cb;

    FmtLambda(::std::function<void(::std::ostream&)> cb)
        : m_cb(cb)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const FmtLambda& x) {
        x.m_cb(os);
        return os;
    }
};

#define FMT_CB(os, ...)         \
    ::FmtLambda([&](auto& os) { \
        __VA_ARGS__;            \
    })
#define FMT_CB_S(...)            \
    ::FmtLambda([&](auto& _os) { \
        _os << __VA_ARGS__;      \
    })
