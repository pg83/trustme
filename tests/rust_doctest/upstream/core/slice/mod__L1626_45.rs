// Extracted from library/core/src/slice/mod.rs:1626
#![allow(unused)]
#![feature(array_windows)]
fn main() {
    let slice = [0, 1, 2, 3];
    let mut iter = slice.array_windows();
    assert_eq!(iter.next().unwrap(), &[0, 1]);
    assert_eq!(iter.next().unwrap(), &[1, 2]);
    assert_eq!(iter.next().unwrap(), &[2, 3]);
    assert!(iter.next().is_none());
}
