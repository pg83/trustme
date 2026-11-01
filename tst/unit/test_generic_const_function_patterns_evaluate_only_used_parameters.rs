//@ compile-variant: -O
use std::marker::PhantomData;
const fn fixed<T>() -> u8 { 17 }
const fn selected<T, const N: u8>() -> u8 { N }
struct Pattern<T>(PhantomData<T>);
impl<T> Pattern<T> {
    const FIRST: u8 = fixed::<T>();
    const LAST: u8 = selected::<T, 23>();
}
fn classify<T>(value: u8) -> u8 {
    match value { Pattern::<T>::FIRST => 1, Pattern::<T>::FIRST..=Pattern::<T>::LAST => 2, _ => 3 }
}
fn main() {
    for value in [0, 16, 17, 19, 23, 24] {
        let expected = if value == 17 { 1 } else if (17..=23).contains(&value) { 2 } else { 3 };
        assert_eq!(classify::<String>(value), expected);
        assert_eq!(classify::<u16>(value), expected);
    }
}
