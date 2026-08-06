// Extracted from library/core/src/slice/mod.rs:4509
#![allow(unused)]
fn main() {
    let mut slice: &mut [_] = &mut ['a', 'b', 'c', 'd'];
    let mut tail = slice.split_off_mut(2..).unwrap();
    
    assert_eq!(slice, &mut ['a', 'b']);
    assert_eq!(tail, &mut ['c', 'd']);
}
