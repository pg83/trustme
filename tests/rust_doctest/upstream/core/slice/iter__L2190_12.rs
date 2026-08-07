// Extracted from library/core/src/slice/iter.rs:2190
#![allow(unused)]
#![feature(array_windows)]
fn main() {

    let slice = [0, 1, 2, 3];
    let mut iter = slice.array_windows::<2>();
    assert_eq!(iter.next(), Some(&[0, 1]));
    assert_eq!(iter.next(), Some(&[1, 2]));
    assert_eq!(iter.next(), Some(&[2, 3]));
    assert_eq!(iter.next(), None);
}
