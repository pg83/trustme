#![feature(decl_macro)]

macro_rules! value {
    ($statement:stmt) => {{ $statement generated!() }};
}

fn main() {
    assert_eq!(value!(macro generated() { 47u16 }), 47);
}
