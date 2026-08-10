// Extracted from library/core/src/slice/index.rs:809
#![allow(unused)]
#![feature(slice_range)]
fn main() {

    use std::slice;

    let v = [10, 40, 30];
    assert_eq!(1..2, slice::range(1..2, ..v.len()));
    assert_eq!(0..2, slice::range(..2, ..v.len()));
    assert_eq!(1..3, slice::range(1.., ..v.len()));
}
