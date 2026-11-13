#![feature(type_ascription, builtin_syntax)]

fn selected() -> &'static u16 {
    &(builtin # type_ascribe([53u16, 59, 61], [u16; 3]))[1]
}

fn main() {
    let value = selected();
    assert_eq!(*value, 59);
    assert_eq!(*selected(), *value);
}
