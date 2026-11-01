//@ compile-variant: -O
use std::marker::PhantomData;
struct Limit<T, const N: usize>(PhantomData<T>);
impl<T, const N: usize> Limit<T, N> { const START: u8 = 13; const END: u8 = 23; }
fn classify<T, const N: usize>(value: u8) -> u8 {
    match value {
        Limit::<T, N>::START => 1,
        Limit::<T, N>::START..=Limit::<T, N>::END => 2,
        _ => 3,
    }
}
fn main() {
    for value in [0, 12, 13, 17, 23, 24] {
        let expected = if value == 13 { 1 } else if (13..=23).contains(&value) { 2 } else { 3 };
        assert_eq!(classify::<String, 3>(value), expected);
        assert_eq!(classify::<u16, 7>(value), expected);
    }
}
