// Extracted from library/core/src/slice/index.rs:830
#![allow(unused)]
#![feature(slice_range)]
fn main() {

    use std::slice;

    let _ = slice::range(1..4, ..3);
}
