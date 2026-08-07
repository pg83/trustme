// Extracted from library/core/src/slice/index.rs:888
#![allow(unused)]
#![feature(slice_range)]
fn main() {

    use std::slice;

    let v = [10, 40, 30];
    assert_eq!(Some(1..2), slice::try_range(1..2, ..v.len()));
    assert_eq!(Some(0..2), slice::try_range(..2, ..v.len()));
    assert_eq!(Some(1..3), slice::try_range(1.., ..v.len()));
}
