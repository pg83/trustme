//@ compile-flags: --cap-lints=allow -A warnings
//@ compile-fail: Type mismatch between
fn accept(_: u8) {}
fn main() { accept(true); }
