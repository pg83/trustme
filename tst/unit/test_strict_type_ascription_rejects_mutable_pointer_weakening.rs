//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let mut value = 37u32;
    let pointer = &mut value as *mut u32;
    let _ = builtin # type_ascribe(pointer, *const u32);
}
