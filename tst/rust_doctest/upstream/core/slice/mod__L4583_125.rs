// Extracted from library/core/src/slice/mod.rs:4583
#![allow(unused)]
fn main() {
    let mut slice: &mut [_] = &mut ['a', 'b', 'c'];
    let first = slice.split_off_first_mut().unwrap();
    *first = 'd';

    assert_eq!(slice, &['b', 'c']);
    assert_eq!(first, &'d');
}
