#![feature(adt_const_params)]
#![allow(incomplete_features)]

struct Digits<const VALUE: (u8, u8)>;

impl<const VALUE: (u8, u8)> Digits<VALUE> {
    const NUMBER: u16 = VALUE.0 as u16 * 10 + VALUE.1 as u16;
}

fn forward<const VALUE: (u8, u8)>() -> ((u8, u8), u16) {
    (VALUE, Digits::<VALUE>::NUMBER)
}

fn main() {
    assert_eq!(forward::<{ (2, 7) }>(), ((2, 7), 27));
    assert_eq!(forward::<{ (7, 2) }>(), ((7, 2), 72));
    assert_eq!(Digits::<{ (0, 9) }>::NUMBER, 9);
}
