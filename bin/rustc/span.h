#pragma once

#include "output.h"

#include "rc_string.h"

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

struct SpanMessageCallback {
    virtual void write(stl::ZeroCopyOutput& os) = 0;
};

template <typename F>
struct SpanMessageCb final: SpanMessageCallback {
    F f;

    explicit SpanMessageCb(F f)
        : f(f)
    {
    }

    void write(stl::ZeroCopyOutput& os) override {
        f(os);
    }
};

struct Span {
private:
    SpanInner* ptr;

public:
    Span();

    Span(Span parent, RcString filename, unsigned int startLine, unsigned int startOfs, unsigned int endLine, unsigned int endOfs);
    Span(Span parent, const Position& position);
    Span(Span parent, RcString sourceCrate, RcString macroName);
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

    const SpanInner* operator->() const {
        return ptr;
    }

    const SpanInnerSource& getTopFileSpan() const;

    [[noreturn]] void bugCb(SpanMessageCallback& msg) const;
    [[noreturn]] void errorCb(ErrorType tag, SpanMessageCallback& msg) const;
    void warningCb(WarningType tag, SpanMessageCallback& msg) const;
    void noteCb(SpanMessageCallback& msg) const;

    template <typename F>
    [[noreturn]] void bug(F f) const {
        SpanMessageCb<F> cb(f);
        bugCb(cb);
    }

    template <typename F>
    [[noreturn]] void error(ErrorType tag, F f) const {
        SpanMessageCb<F> cb(f);
        errorCb(tag, cb);
    }

    template <typename F>
    void warning(WarningType tag, F f) const {
        SpanMessageCb<F> cb(f);
        warningCb(tag, cb);
    }

    template <typename F>
    void note(F f) const {
        SpanMessageCb<F> cb(f);
        noteCb(cb);
    }

private:
    void printSpanMessage(SpanMessageCallback& tag, SpanMessageCallback& msg) const;
};

struct SourceLocation {
    RcString filename;
    unsigned int line = 0;
    unsigned int column = 0;

    SourceLocation() = default;

    SourceLocation(RcString filename, unsigned int line, unsigned int column)
        : filename(std::move(filename))
        , line(line)
        , column(column)
    {
    }

    explicit SourceLocation(const Span& span);

    bool operator==(const SourceLocation& other) const {
        return filename == other.filename && line == other.line && column == other.column;
    }

    bool operator!=(const SourceLocation& other) const {
        return !(*this == other);
    }
};

struct ProtoSpan {
    Span span;
    RcString filename;

    unsigned int startLine;
    unsigned int startOfs;
};

struct SpanInner {
    friend struct Span;

protected:
    size_t referenceCount;

public:
    Span parentSpan;

    virtual ~SpanInner() = 0;
    virtual void fmt(stl::ZeroCopyOutput& os) const = 0;
    virtual RcString crateName() const = 0;
    virtual unsigned int nodeKind() const = 0;
};

struct SpanInnerSource: public SpanInner {
    friend struct Span;

public:
    static constexpr unsigned int kind = 1;
    unsigned int nodeKind() const override;
    RcString filename;

    unsigned int startLine;
    unsigned int startOfs;
    unsigned int endLine;
    unsigned int endOfs;

    ~SpanInnerSource() override;
    void fmt(stl::ZeroCopyOutput& os) const override;

    RcString crateName() const override;

private:
    static SpanInner* alloc(Span parent, RcString filename, unsigned int startLine, unsigned int startOfs, unsigned int endLine, unsigned int endOfs);
};

struct SpanInnerMacro: public SpanInner {
    friend struct Span;
    static constexpr unsigned int kind = 2;
    unsigned int nodeKind() const override;
    RcString crate;
    RcString macro;

    ~SpanInnerMacro() override;
    void fmt(stl::ZeroCopyOutput& os) const override;

    RcString crateName() const override;

private:
    static SpanInner* alloc(Span parent, RcString crate, RcString macro);
};

[[noreturn]] void spanUnreachableAt(const char* file, int line);
#define UNREACHABLE() ::spanUnreachableAt(__FILE__, __LINE__)

template <typename T>
struct Spanned {
    Span sp;
    T ent;
};

template <typename T>
Spanned<T> makeSpanned(Span sp, T val) {
    return Spanned<T>{std::move(sp), std::move(val)};
}

#define ERROR(span, code, msg)                           \
    do {                                                 \
        ::Span(span).error(code, [&](stl::ZeroCopyOutput& os) { \
            os << msg;                                   \
        });                                              \
    } while (0)
#define WARNING(span, code, msg)                           \
    do {                                                   \
        ::Span(span).warning(code, [&](stl::ZeroCopyOutput& os) { \
            os << msg;                                     \
        });                                                \
    } while (0)
#define NOTE(span, msg)                           \
    do {                                          \
        ::Span(span).note([&](stl::ZeroCopyOutput& os) { \
            os << msg;                            \
        });                                       \
    } while (0)
#define BUG(span, msg)                                        \
    do {                                                      \
        ::Span(span).bug([&](stl::ZeroCopyOutput& os) {              \
            os << stl::StringView(__FILE__) << stl::StringView(":") << __LINE__ << stl::StringView(": ") << msg; \
        });                                                   \
    } while (0)
#define TODO(span, msg)                                                                     \
    do {                                                                                    \
        const char* __TODO_func = __func__;                                                 \
        ::Span(span).bug([&](stl::ZeroCopyOutput& os) {                                            \
            os << stl::StringView(__FILE__) << stl::StringView(":") << __LINE__ << stl::StringView(": TODO: ") << __TODO_func << stl::StringView(" - ") << msg; \
        });                                                                                 \
    } while (0)

#define ASSERT_BUG(span, cnd, msg)                                                               \
    do {                                                                                         \
        if (!(cnd)) {                                                                            \
            ::Span(span).bug([&](stl::ZeroCopyOutput& os) {                                             \
                os << stl::StringView("ASSERT FAIL: ") << stl::StringView(__FILE__) << stl::StringView(":") << __LINE__ << stl::StringView(":" #cnd) << stl::StringView(": ") << msg; \
            });                                                                                  \
        }                                                                                        \
    } while (0)
