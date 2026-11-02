//@ compile-variant: -O
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let mut pair = (0u16, 0u16);
    (builtin # type_ascribe(pair.0, u16), pair.1) = (29, 61);
    assert_eq!(pair, (29, 61));
    let mut values = Vec::new();
    builtin # type_ascribe(values, Vec<u8>) = vec![3, 7, 13];
    assert_eq!(values, [3, 7, 13]);
}
