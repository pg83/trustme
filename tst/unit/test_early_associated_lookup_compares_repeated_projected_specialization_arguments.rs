#![feature(specialization)]
#![allow(incomplete_features)]

trait Source {
    type Item;
}

impl Source for i16 {
    type Item = u8;
}

trait Value {
    type Number;
}

struct Plain;
struct Refined;

impl Value for Plain {
    type Number = u16;
}

impl Value for Refined {
    type Number = u32;
}

trait Choose<T, A, B> {
    type Selected: Value;
}

impl<T, U> Choose<T, U, U> for () {
    default type Selected = Plain;
}

impl<T: Source> Choose<T, T::Item, T::Item> for () {
    type Selected = Refined;
}

type Result = <<() as Choose<i16, u8, u8>>::Selected>::Number;

fn main() {
    let value: Result = 541u32;
    assert_eq!(value, 541);
    assert_eq!(std::mem::size_of::<Result>(), 4);
}
