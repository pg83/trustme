// Extracted from library/core/src/slice/mod.rs:2537
#![allow(unused)]
#![feature(slice_split_once)]
fn main() {
    let s = [1, 2, 3, 2, 4];
    assert_eq!(s.rsplit_once(|&x| x == 2), Some((
        &[1, 2, 3][..],
        &[4][..]
    )));
    assert_eq!(s.rsplit_once(|&x| x == 0), None);
}
