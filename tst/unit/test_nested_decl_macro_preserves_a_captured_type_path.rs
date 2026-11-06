#![feature(decl_macro)]

type Word = u16;

macro_rules! define_type {
    ($name:ident, $ty:ty) => {
        macro $name() { $ty }
    };
}

define_type!(selected_type, Word);

fn main() {
    let value: selected_type!() = 701;
    assert_eq!(value, 701u16);
    assert_eq!(std::mem::size_of_val(&value), 2);
}
