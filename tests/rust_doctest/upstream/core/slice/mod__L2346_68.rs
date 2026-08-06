// Extracted from library/core/src/slice/mod.rs:2346
#![allow(unused)]
fn main() {
    let v = &[0, 1, 1, 2, 3, 5, 8];
    let mut it = v.rsplit(|n| *n % 2 == 0);
    assert_eq!(it.next().unwrap(), &[]);
    assert_eq!(it.next().unwrap(), &[3, 5]);
    assert_eq!(it.next().unwrap(), &[1, 1]);
    assert_eq!(it.next().unwrap(), &[]);
    assert_eq!(it.next(), None);
}
