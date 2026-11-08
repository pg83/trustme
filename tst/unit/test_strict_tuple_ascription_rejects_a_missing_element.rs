//@ compile-fail: Tuples are of different length
#![feature(type_ascription, builtin_syntax)]
fn main() {
    let _ = builtin # type_ascribe((3u8, 5u16), (u8, u16, u32));
}
