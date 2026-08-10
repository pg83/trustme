// Extracted from library/core/src/slice/mod.rs:2202
#![allow(unused)]
fn main() {
    let slice = [10, 40, 33, 20];
    let mut iter = slice.split(|num| num % 3 == 0);

    assert_eq!(iter.next().unwrap(), &[10, 40]);
    assert_eq!(iter.next().unwrap(), &[20]);
    assert!(iter.next().is_none());
}
