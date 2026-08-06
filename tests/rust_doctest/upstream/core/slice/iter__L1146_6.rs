// Extracted from library/core/src/slice/iter.rs:1146
#![allow(unused)]
fn main() {
    let slice = [10, 40, 30, 20, 60, 50];
    let mut iter = slice.splitn(2, |num| *num % 3 == 0);
    assert_eq!(iter.next(), Some(&[10, 40][..]));
    assert_eq!(iter.next(), Some(&[20, 60, 50][..]));
    assert_eq!(iter.next(), None);
}
