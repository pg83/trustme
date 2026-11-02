//@ compile-variant: -O
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let item = builtin # type_ascribe((271, 19), (u16, u8));
    let pointer = builtin # type_ascribe(&item.0, &u16);
    assert_eq!(*pointer, 271);
    assert_eq!(item.1.checked_add(237), None);
}
