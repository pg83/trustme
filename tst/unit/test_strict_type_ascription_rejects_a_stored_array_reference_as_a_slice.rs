//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let values = [11u16, 29, 47];
    let reference = &values;
    let _ = builtin # type_ascribe(reference, &[u16]);
}
