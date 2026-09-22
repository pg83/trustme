// `cfg_attr` expands to attributes parsed like any other, and an
// attribute's value after `=` is an expression (upstream's
// expand_cfg_attr parses them with the session's parser). The cfg_attr
// expansion parsed them without a parse state, so a value that is not a
// single literal - zerocopy's `doc = concat!(..)` - crashed the compiler.
// The attribute list, like any upstream sequence, may end in a comma.
#![cfg_attr(all(), doc = concat!("crate ", "docs"))]

#[cfg_attr(all(), doc = concat!("a ", "struct"))]
#[cfg_attr(all(), derive(Clone, Copy),)]
#[cfg_attr(any(), no_such::tool(value <= 3), no_such::other(|&p| p < 1),)]
struct Documented(u8);

#[cfg_attr(any(), doc = concat!("never ", "kept"))]
fn main() {
    let first = Documented(7);
    let second = first;
    assert_eq!(first.0 + second.0, 14);
}
