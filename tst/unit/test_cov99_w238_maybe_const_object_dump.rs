//@ compile-only
//@ compile-flags: -Zdump-ast -Zstop-after=expand
//@ output-contains: _1_ast.rs value: ([const] Marker),

trait Marker {}

struct Holder {
    value: dyn ~const Marker,
}

fn main() {}
