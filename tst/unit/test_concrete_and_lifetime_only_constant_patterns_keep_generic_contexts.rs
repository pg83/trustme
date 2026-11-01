//@ compile-variant: -O
use std::marker::PhantomData;
struct Limit<T, const N: usize>(PhantomData<T>);
impl<T, const N: usize> Limit<T, N> { const START: u8 = 13; const END: u8 = 23; }
struct Borrowed<'a>(&'a ());
impl Borrowed<'_> { const VALUE: u8 = 31; }
trait Pattern { const VALUE: u8; }
impl Pattern for u16 { const VALUE: u8 = 37; }
fn classify<T>(value: u8, _: PhantomData<T>) -> u8 {
    match value {
        Limit::<[u8; 1 + 2], { 2 + 1 }>::START..=Limit::<[u8; 3], 3>::END => 1,
        <u16 as Pattern>::VALUE => 2,
        _ => 3,
    }
}
fn borrowed<'a>(value: u8, input: &'a ()) -> bool {
    let _ = Borrowed(input).0;
    match value { Borrowed::<'a>::VALUE => true, _ => false }
}
fn main() {
    for value in [0, 12, 13, 17, 23, 24, 37] {
        let expected = if (13..=23).contains(&value) { 1 } else if value == 37 { 2 } else { 3 };
        assert_eq!(classify::<String>(value, PhantomData), expected);
    }
    assert!(borrowed(31, &()));
    assert!(!borrowed(13, &()));
}
