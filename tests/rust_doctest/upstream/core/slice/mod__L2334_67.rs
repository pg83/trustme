// Extracted from library/core/src/slice/mod.rs:2334
#![allow(unused)]
fn main() {
    let slice = [11, 22, 33, 0, 44, 55];
    let mut iter = slice.rsplit(|num| *num == 0);
    
    assert_eq!(iter.next().unwrap(), &[44, 55]);
    assert_eq!(iter.next().unwrap(), &[11, 22, 33]);
    assert_eq!(iter.next(), None);
}
