//@ compile-only
#![feature(type_ascription, builtin_syntax, never_type)]

fn read() -> u16 {
    builtin # type_ascribe(!(return 47), !)
}

fn main() {
    assert_eq!(read(), 47);
}
