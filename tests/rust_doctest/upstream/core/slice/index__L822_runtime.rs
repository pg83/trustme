// Extracted from library/core/src/slice/index.rs:822
#![allow(unused)]
#![feature(slice_range)]
fn main() {

    use std::slice;

    let _ = slice::range(2..1, ..3);
}
