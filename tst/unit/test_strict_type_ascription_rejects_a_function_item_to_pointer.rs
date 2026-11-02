//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn increment(value: u16) -> u16 { value + 3 }
fn main() {
    let item = increment;
    let _ = builtin # type_ascribe(item, fn(u16) -> u16);
}
