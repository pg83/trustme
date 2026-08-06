// Extracted from library/core/src/slice/index.rs:838
#![allow(unused)]
#![feature(slice_range)]
fn main() {
    
    use std::slice;
    
    let _ = slice::range(1..=usize::MAX, ..3);
}
