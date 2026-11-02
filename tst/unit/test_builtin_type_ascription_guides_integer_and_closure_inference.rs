//@ compile-fail: type mismatch
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let small = builtin # type_ascribe(250, u8);
    let callback = builtin # type_ascribe(|n| n + 5, fn(u16) -> u16);
    assert_eq!(small.checked_add(6), None);
    assert_eq!(callback(23), 28);
    let pointer = builtin # type_ascribe(&small, &u8);
    assert_eq!(*pointer, 250);
}
