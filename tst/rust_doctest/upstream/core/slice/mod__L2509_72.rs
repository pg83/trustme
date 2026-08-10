// Extracted from library/core/src/slice/mod.rs:2509
#![allow(unused)]
#![feature(slice_split_once)]
fn main() {
    let s = [1, 2, 3, 2, 4];
    assert_eq!(s.split_once(|&x| x == 2), Some((
        &[1][..],
        &[3, 2, 4][..]
    )));
    assert_eq!(s.split_once(|&x| x == 0), None);
}
