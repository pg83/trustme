//@ compile-fail: Unable to find an implementation of Fn*
#![feature(type_ascription, builtin_syntax)]
fn reject<F: FnOnce() -> (u8, u16)>(callback: F) {
    let _ = builtin # type_ascribe(callback(), (u32, _));
}
fn main() {}
