// Extracted from library/core/src/slice/mod.rs:1024
#![allow(unused)]
fn main() {
    let x = &[1, 2, 4];
    let mut iterator = x.iter();

    assert_eq!(iterator.next(), Some(&1));
    assert_eq!(iterator.next(), Some(&2));
    assert_eq!(iterator.next(), Some(&4));
    assert_eq!(iterator.next(), None);
}
