//@ compile-variant: -O
#![feature(fn_delegation, precise_capturing_in_traits)]
#![allow(incomplete_features)]

trait Source: Copy {
    fn items(value: Self) -> impl Iterator<Item = Self> + ?Sized + Sized {
        [value, value].into_iter()
    }
    fn copied(value: Self) -> impl Into<Self> + Copy + use<Self> { value }
}
impl Source for u16 {}
reuse Source::{items, copied};

fn main() {
    assert_eq!(items(17u16).sum::<u16>(), 34);
    let value: u16 = copied(23u16).into();
    assert_eq!(value, 23);
}
