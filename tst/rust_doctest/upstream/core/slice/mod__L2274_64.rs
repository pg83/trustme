// Extracted from library/core/src/slice/mod.rs:2274
#![allow(unused)]
fn main() {
    let slice = [10, 40, 33, 20];
    let mut iter = slice.split_inclusive(|num| num % 3 == 0);

    assert_eq!(iter.next().unwrap(), &[10, 40, 33]);
    assert_eq!(iter.next().unwrap(), &[20]);
    assert!(iter.next().is_none());
}
