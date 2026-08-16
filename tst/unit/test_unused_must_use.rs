//@ compile-fail: unused return value that must be used
// `#[must_use]` asks the caller to use the value, and `unused_must_use` is the
// lint that says so. Under `#![deny(...)]` that has to be a hard error, and the
// lint looks through block expressions to find the discarded call.
//
// Same shape as the Rust Reference examples attributes/diagnostics.md:415 and
// :469.
#![deny(unused_must_use)]

#[must_use]
fn f() {}

fn main() {
    { f() };
}
