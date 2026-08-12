#pragma once

#include "rc_string.h"
#include <functional>
#include <memory>

enum ErrorType {
    E0000,
};

enum WarningType {
    W0000,
};

class Position;
struct SpanInner;
struct SpanInnerSource;

struct Span {
private:
    SpanInner* ptr;
    //static SpanInner    s_empty_span;
public:
    Span();

    Span(Span parent, RcString filename, unsigned int start_line, unsigned int start_ofs, unsigned int endLine, unsigned int endOfs);
    Span(Span parent, const Position& position);
    Span(Span parent, RcString source_crate, RcString macroName);
    ~Span();

    Span(const Span& x);

    Span(Span&& x);

    Span& operator=(const Span& x);

    Span& operator=(Span&& x);

    operator bool() const {
        return ptr != nullptr;
    }

    bool operator==(const Span& x) const {
        return ptr == x.ptr;
    }

    bool operator!=(const Span& x) const {
        return !(*this == x);
    }

    const SpanInner* get() const {
        return ptr;
    }

    //const SpanInner& operator*() const { return *m_ptr; }
    const SpanInner* operator->() const {
        return ptr;
    }

    const SpanInnerSource& getTopFileSpan() const;

    void bug(::std::function<void(::std::ostream&)> msg) const;
    void error(ErrorType tag, ::std::function<void(::std::ostream&)> msg) const;
    void warning(WarningType tag, ::std::function<void(::std::ostream&)> msg) const;
    void note(::std::function<void(::std::ostream&)> msg) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const Span& sp);

private:
    void printSpanMessage(::std::function<void(::std::ostream&)> tag, ::std::function<void(::std::ostream&)> msg) const;
};

struct ProtoSpan {
    // If `span` is populated, then this `ProtoSpan` was from a macro expansion
    Span span;
    RcString filename;

    unsigned int start_line;
    unsigned int start_ofs;
};

struct SpanInner {
    friend struct Span;

protected:
    size_t referenceCount;

public:
    Span parent_span;

    virtual ~SpanInner() = 0;
    virtual void fmt(::std::ostream& os) const = 0;
    virtual RcString crate_name() const = 0;
    virtual unsigned int nodeKind() const = 0;
};

struct SpanInnerSource: public SpanInner {
    friend struct Span;

public:
    static constexpr unsigned int kind = 1;
    unsigned int nodeKind() const override;
    RcString filename;

    unsigned int start_line;
    unsigned int start_ofs;
    unsigned int endLine;
    unsigned int endOfs;

    ~SpanInnerSource() override;
    void fmt(::std::ostream& os) const override;

    RcString crate_name() const override {
        return RcString();
    }

private:
    static SpanInner* alloc(Span parent, RcString filename, unsigned int start_line, unsigned int start_ofs, unsigned int endLine, unsigned int endOfs);
};

struct SpanInnerMacro: public SpanInner {
    friend struct Span;
    static constexpr unsigned int kind = 2;
    unsigned int nodeKind() const override;
    RcString crate;
    RcString macro;

    ~SpanInnerMacro() override;
    void fmt(::std::ostream& os) const override;

    RcString crate_name() const override {
        return crate;
    }

private:
    static SpanInner* alloc(Span parent, RcString crate, RcString macro);
};

template <typename T>
struct Spanned {
    Span sp;
    T ent;
};

template <typename T>
Spanned<T> makeSpanned(Span sp, T val) {
    return Spanned<T>{::std::move(sp), ::std::move(val)};
}

#define ERROR(span, code, msg)                                  \
    do {                                                        \
        ::Span(span).error(code, [&](::std::ostream& os) {      \
            os << msg;                                          \
        });                                                     \
        throw ::std::runtime_error("Error fell through" #code); \
    } while (0)
#define WARNING(span, code, msg)                             \
    do {                                                     \
        ::Span(span).warning(code, [&](::std::ostream& os) { \
            os << msg;                                       \
        });                                                  \
    } while (0)
#define NOTE(span, msg)                             \
    do {                                            \
        ::Span(span).note([&](::std::ostream& os) { \
            os << msg;                              \
        });                                         \
    } while (0)
#define BUG(span, msg)                                        \
    do {                                                      \
        ::Span(span).bug([&](::std::ostream& os) {            \
            os << __FILE__ << ":" << __LINE__ << ": " << msg; \
        });                                                   \
        throw ::std::runtime_error("Bug fell through");       \
    } while (0)
#define TODO(span, msg)                                                                     \
    do {                                                                                    \
        const char* __TODO_func = __func__;                                                 \
        ::Span(span).bug([&](::std::ostream& os) {                                          \
            os << __FILE__ << ":" << __LINE__ << ": TODO: " << __TODO_func << " - " << msg; \
        });                                                                                 \
        throw ::std::runtime_error("Bug (todo) fell through");                              \
    } while (0)

#define ASSERT_BUG(span, cnd, msg)                                                               \
    do {                                                                                         \
        if (!(cnd)) {                                                                            \
            ::Span(span).bug([&](::std::ostream& os) {                                           \
                os << "ASSERT FAIL: " << __FILE__ << ":" << __LINE__ << ":" #cnd << ": " << msg; \
            });                                                                                  \
            throw ::std::runtime_error("Bug fell through");                                      \
        }                                                                                        \
    } while (0)
