#include "span.h"
#include <functional>
#include <iostream>
#include "parse_lex.h"
#include "common.h"

Span::Span(Span parent, RcString filename, unsigned int start_line, unsigned int start_ofs, unsigned int end_line, unsigned int end_ofs)
    : ptr(SpanInnerSource::alloc(parent, ::std::move(filename), start_line, start_ofs, end_line, end_ofs))
{
}

Span::Span(Span parent, const Position& pos)
    : ptr(SpanInnerSource::alloc(parent, pos.filename, pos.line, pos.ofs, pos.line, pos.ofs))
{
}

Span::Span(Span parent, RcString source_crate, RcString macro_name)
    : ptr(SpanInnerMacro::alloc(parent, source_crate, macro_name))
{
}

Span::Span(const Span& x)
    : ptr(x.ptr)
{
    if (ptr) {
        ptr->reference_count += 1;
    }
}

Span::~Span() {
    if (ptr) {
        ptr->reference_count--;
        if (ptr->reference_count == 0) {
            delete ptr;
        }
        ptr = nullptr;
    }
}

const SpanInnerSource& Span::get_top_file_span() const {
    auto* top_span = this;
    while (top_span->get() && (*top_span)->parent_span != Span()) {
        top_span = &(*top_span)->parent_span;
    }
    if (const auto* ts = cast<const SpanInnerSource>(top_span->get())) {
        return *ts;
    }
    TODO(*this, "Top span isn't source?");
}

void Span::print_span_message(::std::function<void(::std::ostream&)> tag, ::std::function<void(::std::ostream&)> msg) const {
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
    print_span_message([](auto& os) {
        os << "BUG";
    }, msg);
    abort();
}

void Span::error(ErrorType tag, ::std::function<void(::std::ostream&)> msg) const {
    print_span_message([&](auto& os) {
        os << "error:" << tag;
    }, msg);
    abort();
}

void Span::warning(WarningType tag, ::std::function<void(::std::ostream&)> msg) const {
    print_span_message([&](auto& os) {
        os << "warn:" << tag;
    }, msg);
}

void Span::note(::std::function<void(::std::ostream&)> msg) const {
    print_span_message([](auto& os) {
        os << "note";
    }, msg);
}

SpanInner::~SpanInner() {
}

SpanInnerSource::~SpanInnerSource() {
}

unsigned int SpanInnerSource::node_kind() const {
    return SpanInnerSource::kind;
}

void SpanInnerSource::fmt(::std::ostream& os) const {
    os << this->filename;
    if (this->start_line != this->end_line) {
        os << ":" << this->start_line << "-" << this->end_line;
    } else if (this->start_ofs != this->end_ofs) {
        os << ":" << this->start_line << ":" << this->start_ofs << "-" << this->end_ofs;
    } else {
        os << ":" << this->start_line << ":" << this->start_ofs;
    }
}

SpanInnerMacro::~SpanInnerMacro() {
}

unsigned int SpanInnerMacro::node_kind() const {
    return SpanInnerMacro::kind;
}

void SpanInnerMacro::fmt(::std::ostream& os) const {
    os << "MACRO<::\"" << this->crate << "\"::" << this->macro << ">";
}

/*static*/ SpanInner* SpanInnerMacro::alloc(Span parent, RcString crate, RcString macro) {
    auto rv = new SpanInnerMacro;
    rv->reference_count = 1;
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
SpanInner* SpanInnerSource::alloc(Span parent, RcString filename, unsigned int start_line, unsigned int start_ofs, unsigned int end_line, unsigned int end_ofs) {
    auto* rv = new SpanInnerSource();
    rv->reference_count = 1;
    rv->parent_span = parent;
    rv->filename = ::std::move(filename);
    rv->start_line = start_line;
    rv->start_ofs = start_ofs;
    rv->end_line = end_line;
    rv->end_ofs = end_ofs;
    return rv;
}
