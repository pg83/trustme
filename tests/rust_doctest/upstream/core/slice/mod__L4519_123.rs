// Extracted from library/core/src/slice/mod.rs:4519
#![allow(unused)]
fn main() {
    let mut slice: &mut [_] = &mut ['a', 'b', 'c', 'd'];
    
    assert_eq!(None, slice.split_off_mut(5..));
    assert_eq!(None, slice.split_off_mut(..5));
    assert_eq!(None, slice.split_off_mut(..=4));
    let expected: &mut [_] = &mut ['a', 'b', 'c', 'd'];
    assert_eq!(Some(expected), slice.split_off_mut(..4));
}
