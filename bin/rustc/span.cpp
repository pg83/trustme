#include "span.h"
#include <functional>
#include <iostream>
#include "parse_lex.h"
#include "common.h"

Span::Span(Span parent, RcString filename, unsigned int startLine, unsigned int startOfs, unsigned int endLine, unsigned int endOfs)
    : ptr(SpanInnerSource::alloc(parent, ::std::move(filename), startLine, startOfs, endLine, endOfs))
{
}

Span::Span(Span parent, const Position& pos)
    : ptr(SpanInnerSource::alloc(parent, pos.filename, pos.line, pos.ofs, pos.line, pos.ofs))
{
}

Span::Span(Span parent, RcString source_crate, RcString macroName)
    : ptr(SpanInnerMacro::alloc(parent, source_crate, macroName))
{
}

Span::Span(const Span& x)
    : ptr(x.ptr)
{
    if (ptr) {
        ptr->referenceCount += 1;
    }
}

Span::~Span() {
    if (ptr) {
        ptr->referenceCount--;
        if (ptr->referenceCount == 0) {
            delete ptr;
        }
        ptr = nullptr;
    }
}

const SpanInnerSource& Span::getTopFileSpan() const {
    auto* topSpan = this;
    while (topSpan->get() && (*topSpan)->parent_span != Span()) {
        topSpan = &(*topSpan)->parent_span;
    }
    if (const auto* ts = cast<const SpanInnerSource>(topSpan->get())) {
        return *ts;
    }
    TODO(*this, "Top span isn't source?");
}

void Span::printSpanMessage(::std::function<void(::std::ostream&)> tag, ::std::function<void(::std::ostream&)> msg) const {
    const Span& sp = *this;
    auto& sink = ::std::cerr;
    sink << sp << " ";
    //sink << sp->filename << ":" << sp->start_line << ": ";
    tag(sink);
    sink << ": ";
    msg(sink);
    sink << ::std::endl;

    if (sp.get()) {
        for (auto parent = sp->parent_span; parent != Span(); parent = parent->parent_span) {
            sink << parent << ": note: From here" << ::std::endl;
        }
    }

    sink << ::std::flush;
}

void Span::bug(::std::function<void(::std::ostream&)> msg) const {
    printSpanMessage([](auto& os) {
        os << "BUG";
    }, msg);
    abort();
}

void Span::error(ErrorType tag, ::std::function<void(::std::ostream&)> msg) const {
    printSpanMessage([&](auto& os) {
        os << "error:" << tag;
    }, msg);
    abort();
}

void Span::warning(WarningType tag, ::std::function<void(::std::ostream&)> msg) const {
    printSpanMessage([&](auto& os) {
        os << "warn:" << tag;
    }, msg);
}

void Span::note(::std::function<void(::std::ostream&)> msg) const {
    printSpanMessage([](auto& os) {
        os << "note";
    }, msg);
}

SpanInner::~SpanInner() {
}

SpanInnerSource::~SpanInnerSource() {
}

unsigned int SpanInnerSource::nodeKind() const {
    return SpanInnerSource::kind;
}

void SpanInnerSource::fmt(::std::ostream& os) const {
    os << this->filename;
    if (this->startLine != this->endLine) {
        os << ":" << this->startLine << "-" << this->endLine;
    } else if (this->startOfs != this->endOfs) {
        os << ":" << this->startLine << ":" << this->startOfs << "-" << this->endOfs;
    } else {
        os << ":" << this->startLine << ":" << this->startOfs;
    }
}

SpanInnerMacro::~SpanInnerMacro() {
}

unsigned int SpanInnerMacro::nodeKind() const {
    return SpanInnerMacro::kind;
}

void SpanInnerMacro::fmt(::std::ostream& os) const {
    os << "MACRO<::\"" << this->crate << "\"::" << this->macro << ">";
}

/*static*/ SpanInner* SpanInnerMacro::alloc(Span parent, RcString crate, RcString macro) {
    auto rv = new SpanInnerMacro;
    rv->referenceCount = 1;
    rv->parent_span = std::move(parent);
    rv->crate = std::move(crate);
    rv->macro = std::move(macro);
    return rv;
}

::std::ostream& operator<<(::std::ostream& os, const Span& sp) {
    if (sp.ptr) {
        sp.ptr->fmt(os);
    } else {
        os << "<null>";
    }
    return os;
}

Span::Span()
    //: m_ptr(&s_empty_span)
    : ptr(nullptr) {
}
Span::Span(Span&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
Span& Span::operator=(const Span& x) {
    this->~Span();
    new (this) Span(x);
    return *this;
}
Span& Span::operator=(Span&& x) {
    this->~Span();
    new (this) Span(std::move(x));
    return *this;
}
SpanInner* SpanInnerSource::alloc(Span parent, RcString filename, unsigned int startLine, unsigned int startOfs, unsigned int endLine, unsigned int endOfs) {
    auto* rv = new SpanInnerSource();
    rv->referenceCount = 1;
    rv->parent_span = parent;
    rv->filename = ::std::move(filename);
    rv->startLine = startLine;
    rv->startOfs = startOfs;
    rv->endLine = endLine;
    rv->endOfs = endOfs;
    return rv;
}
