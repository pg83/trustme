// Extracted from library/core/src/slice/iter.rs:546
#![allow(unused)]
fn main() {
    let slice = [10, 40, 33, 20];
    let mut iter = slice.split_inclusive(|num| num % 3 == 0);
    assert_eq!(iter.next(), Some(&[10, 40, 33][..]));
    assert_eq!(iter.next(), Some(&[20][..]));
    assert_eq!(iter.next(), None);
}
