//@ compile-fail: Type mismatch between
#![feature(type_ascription, builtin_syntax, never_type)]
fn reject<F: FnOnce() -> !>(callback: F) {
    let _ = builtin # type_ascribe(callback(), (u32, _));
}
fn main() {}
