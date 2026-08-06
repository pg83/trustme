// Extracted from library/core/src/slice/mod.rs:2228
#![allow(unused)]
fn main() {
    let slice = [10, 6, 33, 20];
    let mut iter = slice.split(|num| num % 3 == 0);
    
    assert_eq!(iter.next().unwrap(), &[10]);
    assert_eq!(iter.next().unwrap(), &[]);
    assert_eq!(iter.next().unwrap(), &[20]);
    assert!(iter.next().is_none());
}
