//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let callback = || 71u32;
    let _ = builtin # type_ascribe(callback, fn() -> u32);
}
