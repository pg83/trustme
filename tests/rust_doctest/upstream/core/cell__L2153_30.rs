// Extracted from library/core/src/cell.rs:2153
#![allow(unused)]
#![feature(unsafe_cell_access)]
fn main() {
    use std::cell::UnsafeCell;

    let uc = UnsafeCell::new(5);

    let old = unsafe { uc.replace(10) };
    assert_eq!(old, 5);
}
