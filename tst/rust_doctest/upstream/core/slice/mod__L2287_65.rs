// Extracted from library/core/src/slice/mod.rs:2287
#![allow(unused)]
fn main() {
    let slice = [3, 10, 40, 33];
    let mut iter = slice.split_inclusive(|num| num % 3 == 0);

    assert_eq!(iter.next().unwrap(), &[3]);
    assert_eq!(iter.next().unwrap(), &[10, 40, 33]);
    assert!(iter.next().is_none());
}
