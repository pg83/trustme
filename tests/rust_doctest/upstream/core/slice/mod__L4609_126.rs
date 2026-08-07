// Extracted from library/core/src/slice/mod.rs:4609
#![allow(unused)]
fn main() {
    let mut slice: &[_] = &['a', 'b', 'c'];
    let last = slice.split_off_last().unwrap();

    assert_eq!(slice, &['a', 'b']);
    assert_eq!(last, &'c');
}
