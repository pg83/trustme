//@ compile-variant: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

#[inline(never)]
fn inspect(values: [u16]) -> u16 {
    let length = values.len();
    if length == 0 { return 7; }
    values[0] * 3 + values[length - 1] * 5 + length as u16
}
fn main() {
    let first: Box<[u16]> = Box::new([3, 5, 7, 11]);
    assert_eq!(inspect(*first), 68);
    let second: Box<[u16]> = Box::new([13, 17]);
    assert_eq!(inspect(*second), 126);
    let empty: Box<[u16]> = Box::new([]);
    assert_eq!(inspect(*empty), 7);
}
