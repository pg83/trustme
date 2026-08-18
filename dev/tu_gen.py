#!/usr/bin/env python3
"""Generate tagged-union classes from a .tu description.

A `xxx.tu` file is Python executed with two names in scope:

    generate(
        name="HIRArraySize",
        default="Unevaluated",
        variants=[
            v("Unevaluated", "HIRConstGeneric", doc="Un-evaluated size"),
            v("Known", "uint64_t", doc="Fully known"),
        ],
        extra_fields=[("uint8_t", "flags", "0")],
        extra="HIRArraySize clone() const;",
        allow_incomplete=False,
    )

Each generate() call emits one class into the output pair:

  - `xxx_tu.h` is thin: type definitions and member declarations only, no
    includes.  The hand-written `xxx.h` provides everything the payload
    types need and then includes `xxx_tu.h`.
  - `xxx_tu.cpp` holds every method body.  It includes `xxx.h`, so payload
    types that were incomplete in the header are complete here.

Storage comes in two shapes.  The default is the classic in-place union of
all payloads, which requires every payload type to be complete at the point
`xxx_tu.h` is included.  With `allow_incomplete=True` the class stores a
single owning `void*` and heap-allocates the payload, so payload types only
need to be *declared* before the include — this is what recursive unions
use, and it makes moves a pointer swap.  Empty variants of such a union are
not allocated at all: they share one static instance per variant.

The generated member surface matches the historical TAGGED_UNION macros
(`TAG_X`, `is_X`, `as_X`, `opt_X`, `make_X`, `unwrap_X`, `tag()`,
`tagStr()`, `tagToStr()`, `Data_X`, `TAGDEAD` as the moved-from state), so
match-side macros and call sites do not change.
"""

import pathlib
import re
import sys

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


class TuError(RuntimeError):
    pass


class Variant:
    def __init__(self, tag, type=None, *, fields=None, copy=True, deep=False,
                 doc=None):
        if not IDENT.match(tag):
            raise TuError(f"variant tag {tag!r} is not an identifier")
        if type is not None and fields is not None:
            raise TuError(f"variant {tag}: pass either a type or fields, not both")
        if fields is not None:
            for field in fields:
                if len(field) not in (2, 3) or not IDENT.match(field[1]):
                    raise TuError(f"variant {tag}: field {field!r} must be"
                                  " (type, name) or (type, name, init)")
        if isinstance(deep, list):
            names = {field[1] for field in (fields or [])}
            for name in deep:
                if name not in names:
                    raise TuError(f"variant {tag}: deep field {name!r} unknown")
        elif deep is True and fields is not None:
            raise TuError(f"variant {tag}: deep=True is for single payloads;"
                          " use deep=[names] with fields")
        self.tag = tag
        self.type = type
        self.fields = fields
        self.copy = copy
        self.deep = deep
        self.doc = doc

    @property
    def data_name(self):
        return f"Data_{self.tag}"

    @property
    def is_empty(self):
        return self.type is None and not self.fields


class Union:
    def __init__(self, *, name, default, variants, extra="", extra_fields=(),
                 allow_incomplete=False, clone=True, doc=None):
        if not IDENT.match(name):
            raise TuError(f"union name {name!r} is not an identifier")
        if not variants:
            raise TuError(f"union {name}: no variants")
        tags = [variant.tag for variant in variants]
        if len(set(tags)) != len(tags):
            raise TuError(f"union {name}: duplicate variant tags")
        if default not in tags:
            raise TuError(f"union {name}: default {default!r} is not a variant")
        for field in extra_fields:
            if len(field) not in (2, 3) or not IDENT.match(field[1]):
                raise TuError(f"union {name}: extra field {field!r} must be"
                              " (type, name) or (type, name, init)")
        self.name = name
        self.default = default
        self.variants = list(variants)
        self.extra = extra
        self.extra_fields = list(extra_fields)
        self.allow_incomplete = allow_incomplete
        self.clone = clone
        self.doc = doc


def load(path):
    unions = []

    def generate(**kwargs):
        unions.append(Union(**kwargs))

    code = compile(path.read_text(encoding="utf-8"), str(path), "exec")
    try:
        exec(code, {"generate": generate, "v": Variant})
    except TuError as error:
        raise SystemExit(f"{path}: {error}")
    if not unions:
        raise SystemExit(f"{path}: no generate() calls")
    return unions


class Writer:
    def __init__(self):
        self.lines = []
        self.depth = 0

    def line(self, text=""):
        indent = "    " * self.depth if text else ""
        self.lines.append(indent + text)

    def open(self, text):
        self.line(text)
        self.depth += 1

    def close(self, text="}"):
        self.depth -= 1
        self.line(text)

    def text(self):
        return "\n".join(self.lines) + "\n"


def doc_lines(out, doc):
    if doc:
        for line in doc.strip().splitlines():
            out.line(f"/// {line.strip()}")


def payload_store(union, variant):
    """The expression naming the live payload inside method bodies."""
    if union.allow_incomplete:
        if variant.is_empty:
            return singleton_name(union, variant)
        return f"*static_cast<{variant.data_name}*>(ptr_)"
    return f"data_.{variant.tag}"


def singleton_name(union, variant):
    return f"empty{union.name}{variant.tag}"


def extra_field_inits(union):
    return "".join(f"\n    , {name}(x.{name})"
                   for _, name, *rest in union.extra_fields)


def emit_header_union(out, union):
    doc_lines(out, union.doc)
    out.open(f"class {union.name} {{")
    out.close("public:")
    out.depth += 1
    for variant in union.variants:
        doc_lines(out, variant.doc)
        if variant.type is not None:
            out.line(f"using {variant.data_name} = {variant.type};")
        else:
            out.open(f"struct {variant.data_name} {{")
            for field in (variant.fields or []):
                type_, name, *init = field
                suffix = f" = {init[0]}" if init else ""
                out.line(f"{type_} {name}{suffix};")
            out.close("};")
    out.line()
    out.open("enum Tag {")
    out.line("TAGDEAD,")
    for variant in union.variants:
        out.line(f"TAG_{variant.tag},")
    out.close("};")
    out.line()
    out.depth -= 1
    out.line("private:")
    out.depth += 1
    out.line("Tag tag_;")
    if union.allow_incomplete:
        out.line("void* ptr_;")
    else:
        out.open("union DataUnion {")
        for variant in union.variants:
            out.line(f"{variant.data_name} {variant.tag};")
        out.open("DataUnion() {")
        out.close()
        out.open("~DataUnion() {")
        out.close()
        out.close("} data_;")
    out.line("void dropPayload();")
    out.line()
    out.depth -= 1
    out.line("public:")
    out.depth += 1
    out.line(f"{union.name}();")
    out.line(f"{union.name}(const {union.name}&) = delete;")
    out.line(f"{union.name}({union.name}&& x) noexcept;")
    out.line(f"{union.name}& operator=({union.name}&& x);")
    out.line(f"~{union.name}();")
    out.line()
    out.line("Tag tag() const;")
    out.line("const char* tagStr() const;")
    out.line("static const char* tagToStr(Tag tag);")
    if union.clone:
        out.line(f"{union.name} clone() const;")
    for variant in union.variants:
        data = variant.data_name
        out.line()
        out.line(f"{union.name}({data}&& v);")
        out.line(f"static {union.name} make_{variant.tag}({data}&& v);")
        if variant.copy:
            out.line(f"{union.name}(const {data}& v);")
            out.line(f"static {union.name} make_{variant.tag}(const {data}& v);")
        out.line(f"bool is_{variant.tag}() const;")
        out.line(f"const {data}* opt_{variant.tag}() const;")
        out.line(f"{data}* opt_{variant.tag}();")
        out.line(f"const {data}& as_{variant.tag}() const;")
        out.line(f"{data}& as_{variant.tag}();")
        out.line(f"{data} unwrap_{variant.tag}();")
    for field in union.extra_fields:
        type_, name, *init = field
        suffix = f" = {init[0]}" if init else ""
        out.line()
        out.line(f"{type_} {name}{suffix};")
    if union.extra.strip():
        out.line()
        for line in union.extra.strip().splitlines():
            out.line(line.strip())
    out.close("};")


def emit_payload_switch(out, union, per_variant):
    """A switch over tag_ running per_variant(variant) for live payloads."""
    out.open("switch (tag_) {")
    out.open("case TAGDEAD: {")
    out.line("break;")
    out.close()
    for variant in union.variants:
        body = per_variant(variant)
        out.open(f"case TAG_{variant.tag}: {{")
        if body:
            out.line(body)
        out.line("break;")
        out.close()
    out.close()


def clone_expr(variant, source):
    """The expression cloning `source` (the live payload of `variant`)."""
    if variant.is_empty:
        return "{}"
    if variant.fields is None:
        if variant.deep is True:
            return f"{source}->clone()"
        return f"tuClone({source})"
    parts = []
    deep = variant.deep if isinstance(variant.deep, list) else []
    for field in variant.fields:
        field_name = field[1]
        if field_name in deep:
            parts.append(f"{source}.{field_name}->clone()")
        else:
            parts.append(f"tuClone({source}.{field_name})")
    return f"{variant.data_name}{{{', '.join(parts)}}}"


def emit_clone_helper(out):
    # Per-value clone: prefer the house clone() member, recurse through
    # vectors, and fall back to copy construction (direct-init, so explicit
    # copy constructors qualify).
    out.open("namespace {")
    out.line("template <typename T>")
    out.line("T tuClone(const T& v);")
    out.line("template <typename E>")
    out.line("::std::vector<E> tuClone(const ::std::vector<E>& v);")
    out.line()
    out.line("template <typename T>")
    out.open("T tuClone(const T& v) {")
    out.open("if constexpr (requires { { v.clone() } -> ::std::convertible_to<T>; }) {")
    out.line("return v.clone();")
    out.close("} else {")
    out.depth += 1
    out.line("T out(v);")
    out.line("return out;")
    out.close()
    out.close()
    out.line()
    out.line("template <typename E>")
    out.open("::std::vector<E> tuClone(const ::std::vector<E>& v) {")
    out.line("::std::vector<E> out;")
    out.line("out.reserve(v.size());")
    out.open("for (const E& e : v) {")
    out.line("out.push_back(tuClone(e));")
    out.close()
    out.line("return out;")
    out.close()
    out.close()


def emit_cpp_union(out, union):
    name = union.name
    default = next(v for v in union.variants if v.tag == union.default)

    if union.allow_incomplete:
        for variant in union.variants:
            if variant.is_empty:
                out.open("namespace {")
                out.line(f"{name}::{variant.data_name} {singleton_name(union, variant)};")
                out.close()
        default_ptr = ("nullptr" if default.is_empty
                       else f"new {default.data_name}()")
        out.line(f"{name}::{name}()")
        out.line(f"    : tag_(TAG_{union.default})")
        out.line(f"    , ptr_({default_ptr})")
        out.open("{")
        out.close()
    else:
        out.line(f"{name}::{name}()")
        out.line(f"    : tag_(TAG_{union.default})")
        out.open("{")
        out.line(f"new (&data_.{union.default}) {default.data_name}();")
        out.close()
    out.line()

    # Move construction transfers the payload's resources and marks the
    # source TAGDEAD; the husk left in the source is never destructed
    # (matching the TAGGED_UNION macros).
    out.line(f"{name}::{name}({name}&& x) noexcept")
    out.line(f"    : tag_(x.tag_){extra_field_inits(union)}")
    out.open("{")
    if union.allow_incomplete:
        out.line("ptr_ = x.ptr_;")
        out.line("x.ptr_ = nullptr;")
    else:
        emit_payload_switch(
            out, union,
            lambda v: f"new (&data_.{v.tag}) {v.data_name}"
                      f"(::std::move(x.data_.{v.tag}));")
    out.line("x.tag_ = TAGDEAD;")
    out.close()
    out.line()

    out.open(f"{name}& {name}::operator=({name}&& x) {{")
    out.open("if (this != &x) {")
    out.line("dropPayload();")
    out.line("tag_ = x.tag_;")
    for _, field_name, *rest in union.extra_fields:
        out.line(f"{field_name} = x.{field_name};")
    if union.allow_incomplete:
        out.line("ptr_ = x.ptr_;")
        out.line("x.ptr_ = nullptr;")
    else:
        emit_payload_switch(
            out, union,
            lambda v: f"new (&data_.{v.tag}) {v.data_name}"
                      f"(::std::move(x.data_.{v.tag}));")
    out.line("x.tag_ = TAGDEAD;")
    out.close()
    out.line("return *this;")
    out.close()
    out.line()

    out.open(f"void {name}::dropPayload() {{")
    if union.allow_incomplete:
        emit_payload_switch(
            out, union,
            lambda v: None if v.is_empty
            else f"delete static_cast<{v.data_name}*>(ptr_);")
        out.line("ptr_ = nullptr;")
    else:
        emit_payload_switch(
            out, union,
            lambda v: f"data_.{v.tag}.~{v.data_name}();")
    out.close()
    out.line()

    out.open(f"{name}::~{name}() {{")
    out.line("dropPayload();")
    out.line("tag_ = TAGDEAD;")
    out.close()
    out.line()

    out.open(f"{name}::Tag {name}::tag() const {{")
    out.line("return tag_;")
    out.close()
    out.line()
    out.open(f"const char* {name}::tagStr() const {{")
    out.line("return tagToStr(tag_);")
    out.close()
    out.line()
    if union.clone:
        out.open(f"{name} {name}::clone() const {{")
        out.line(f"{name} result;")
        out.open("switch (tag_) {")
        out.open("case TAGDEAD: {")
        out.line("break;")
        out.close()
        for variant in union.variants:
            out.open(f"case TAG_{variant.tag}: {{")
            if variant.is_empty:
                out.line(f"result = make_{variant.tag}({{}});")
            else:
                out.line(f"const {variant.data_name}& e = as_{variant.tag}();")
                out.line(f"result = make_{variant.tag}({clone_expr(variant, 'e')});")
            out.line("break;")
            out.close()
        out.close()
        for _, field_name, *rest in union.extra_fields:
            out.line(f"result.{field_name} = {field_name};")
        out.line("return result;")
        out.close()
        out.line()

    out.open(f"const char* {name}::tagToStr(Tag tag) {{")
    out.open("switch (tag) {")
    out.open("case TAGDEAD: {")
    out.line('return "ERR:DEAD";')
    out.close()
    for variant in union.variants:
        out.open(f"case TAG_{variant.tag}: {{")
        out.line(f'return "{variant.tag}";')
        out.close()
    out.close()
    out.line('return "";')
    out.close()

    for variant in union.variants:
        tag = variant.tag
        data = f"{name}::{variant.data_name}"
        store = payload_store(union, variant)
        if union.allow_incomplete:
            init = ("nullptr" if variant.is_empty
                    else f"new {variant.data_name}(::std::move(v))")
            construct_move = [f"    , ptr_({init})"]
            init_copy = ("nullptr" if variant.is_empty
                         else f"new {variant.data_name}(v)")
            construct_copy = [f"    , ptr_({init_copy})"]
            body_move = []
            body_copy = []
        else:
            construct_move = []
            construct_copy = []
            body_move = [f"new (&data_.{tag}) {variant.data_name}(::std::move(v));"]
            body_copy = [f"new (&data_.{tag}) {variant.data_name}(v);"]

        out.line()
        out.line(f"{name}::{name}({data}&& v)")
        out.line(f"    : tag_(TAG_{tag})")
        for line in construct_move:
            out.line(line)
        out.open("{")
        for line in body_move:
            out.line(line)
        if union.allow_incomplete and variant.is_empty:
            out.line("(void)v;")
        out.close()
        out.line()
        out.open(f"{name} {name}::make_{tag}({data}&& v) {{")
        out.line(f"return {name}(::std::move(v));")
        out.close()
        if variant.copy:
            out.line()
            out.line(f"{name}::{name}(const {data}& v)")
            out.line(f"    : tag_(TAG_{tag})")
            for line in construct_copy:
                out.line(line)
            out.open("{")
            for line in body_copy:
                out.line(line)
            if union.allow_incomplete and variant.is_empty:
                out.line("(void)v;")
            out.close()
            out.line()
            out.open(f"{name} {name}::make_{tag}(const {data}& v) {{")
            out.line(f"return {name}(v);")
            out.close()
        out.line()
        out.open(f"bool {name}::is_{tag}() const {{")
        out.line(f"return tag_ == TAG_{tag};")
        out.close()
        out.line()
        out.open(f"const {data}* {name}::opt_{tag}() const {{")
        out.open(f"if (tag_ == TAG_{tag}) {{")
        out.line(f"return &{store};")
        out.close()
        out.line("return nullptr;")
        out.close()
        out.line()
        out.open(f"{data}* {name}::opt_{tag}() {{")
        out.open(f"if (tag_ == TAG_{tag}) {{")
        out.line(f"return &{store};")
        out.close()
        out.line("return nullptr;")
        out.close()
        out.line()
        out.open(f"const {data}& {name}::as_{tag}() const {{")
        out.line(f"assert(tag_ == TAG_{tag});")
        out.line(f"return {store};")
        out.close()
        out.line()
        out.open(f"{data}& {name}::as_{tag}() {{")
        out.line(f"assert(tag_ == TAG_{tag});")
        out.line(f"return {store};")
        out.close()
        out.line()
        out.open(f"{data} {name}::unwrap_{tag}() {{")
        out.line(f"return ::std::move(this->as_{tag}());")
        out.close()


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: tu_gen.py INPUT.tu OUTPUT_tu.h OUTPUT_tu.cpp")
    input_path = pathlib.Path(sys.argv[1])
    header_path = pathlib.Path(sys.argv[2])
    cpp_path = pathlib.Path(sys.argv[3])
    if not input_path.name.endswith(".tu"):
        raise SystemExit(f"{input_path}: input must be a .tu file")
    stem = input_path.name[:-len(".tu")]

    unions = load(input_path)
    banner = f"// Generated by dev/tu_gen.py from {input_path.name}. Do not edit."

    header = Writer()
    header.line(banner)
    header.line("#pragma once")
    for union in unions:
        header.line()
        emit_header_union(header, union)

    cpp = Writer()
    cpp.line(banner)
    cpp.line(f'#include "{stem}.h"')
    cpp.line()
    cpp.line("#include <cassert>")
    cpp.line("#include <new>")
    cpp.line("#include <utility>")
    if any(union.clone for union in unions):
        cpp.line("#include <concepts>")
        cpp.line("#include <vector>")
        cpp.line()
        emit_clone_helper(cpp)
    for union in unions:
        cpp.line()
        emit_cpp_union(cpp, union)

    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(header.text(), encoding="utf-8")
    cpp_path.parent.mkdir(parents=True, exist_ok=True)
    cpp_path.write_text(cpp.text(), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
