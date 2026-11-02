//@ compile-variant: -O
#![feature(type_ascription, builtin_syntax)]
fn identity<T>(value: T) -> T { builtin # type_ascribe(value, T) }
fn increment(value: u16) -> u16 { value + 13 }
fn main() {
    let callback = builtin # type_ascribe(increment as fn(u16) -> u16, fn(u16) -> u16);
    assert_eq!(callback(19), 32);
    let values = identity([41u16, 67, 89]);
    let array = builtin # type_ascribe(&values, &[u16; 3]);
    let slice: &[u16] = array;
    assert_eq!(slice, &[41, 67, 89]);
    let tuple = builtin # type_ascribe((101, 127), (u8, u16));
    assert_eq!(tuple, (101, 127));
}
