// Extracted from library/core/src/slice/mod.rs:4453
#![allow(unused)]
fn main() {
    let mut slice: &[_] = &['a', 'b', 'c', 'd'];
    
    assert_eq!(None, slice.split_off(5..));
    assert_eq!(None, slice.split_off(..5));
    assert_eq!(None, slice.split_off(..=4));
    let expected: &[char] = &['a', 'b', 'c', 'd'];
    assert_eq!(Some(expected), slice.split_off(..4));
}
