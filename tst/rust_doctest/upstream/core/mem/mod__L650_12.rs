// Extracted from library/core/src/mem/mod.rs:650
#![allow(unused)]
fn main() {
    use std::mem;

    let x: i32 = unsafe { mem::zeroed() };
    assert_eq!(0, x);
}
