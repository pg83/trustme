// Extracted from library/core/src/slice/mod.rs:4443
#![allow(unused)]
fn main() {
    let mut slice: &[_] = &['a', 'b', 'c', 'd'];
    let mut tail = slice.split_off(2..).unwrap();

    assert_eq!(slice, &['a', 'b']);
    assert_eq!(tail, &['c', 'd']);
}
