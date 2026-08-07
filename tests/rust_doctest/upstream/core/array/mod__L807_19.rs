// Extracted from library/core/src/array/mod.rs:807
#![allow(unused)]
#![feature(split_array)]
fn main() {

    let mut v = [1, 0, 3, 0, 5, 6];
    let (left, right) = v.rsplit_array_mut::<4>();
    assert_eq!(left, &mut [1, 0]);
    assert_eq!(right, &mut [3, 0, 5, 6][..]);
    left[1] = 2;
    right[1] = 4;
    assert_eq!(v, [1, 2, 3, 4, 5, 6]);
}
