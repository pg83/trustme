// Extracted from library/core/src/slice/iter.rs:1190
#![allow(unused)]
fn main() {
    let slice = [10, 40, 30, 20, 60, 50];
    let mut iter = slice.rsplitn(2, |num| *num % 3 == 0);
    assert_eq!(iter.next(), Some(&[50][..]));
    assert_eq!(iter.next(), Some(&[10, 40, 30, 20][..]));
    assert_eq!(iter.next(), None);
}
