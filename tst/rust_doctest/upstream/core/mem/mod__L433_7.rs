// Extracted from library/core/src/mem/mod.rs:433
#![allow(unused)]
#![allow(deprecated)]
fn main() {
    use std::mem;

    assert_eq!(4, mem::min_align_of::<i32>());
}
