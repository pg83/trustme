// Extracted from library/core/src/slice/mod.rs:4433
#![allow(unused)]
fn main() {
    let mut slice: &[_] = &['a', 'b', 'c', 'd'];
    let mut first_three = slice.split_off(..3).unwrap();

    assert_eq!(slice, &['d']);
    assert_eq!(first_three, &['a', 'b', 'c']);
}
