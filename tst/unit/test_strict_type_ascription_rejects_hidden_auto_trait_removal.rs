//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let value = 19u32;
    let marked: &(dyn std::fmt::Debug + Send) = &value;
    let _ = builtin # type_ascribe(marked, &dyn std::fmt::Debug);
}
