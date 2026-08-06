// Extracted from library/core/src/slice/mod.rs:4499
#![allow(unused)]
fn main() {
    let mut slice: &mut [_] = &mut ['a', 'b', 'c', 'd'];
    let mut first_three = slice.split_off_mut(..3).unwrap();
    
    assert_eq!(slice, &mut ['d']);
    assert_eq!(first_three, &mut ['a', 'b', 'c']);
}
