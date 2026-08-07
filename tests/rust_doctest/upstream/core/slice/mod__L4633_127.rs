// Extracted from library/core/src/slice/mod.rs:4633
#![allow(unused)]
fn main() {
    let mut slice: &mut [_] = &mut ['a', 'b', 'c'];
    let last = slice.split_off_last_mut().unwrap();
    *last = 'd';

    assert_eq!(slice, &['a', 'b']);
    assert_eq!(last, &'d');
}
