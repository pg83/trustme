//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let mut value = 31u32;
    let reference = &mut value;
    let _ = builtin # type_ascribe(reference, &u32);
}
