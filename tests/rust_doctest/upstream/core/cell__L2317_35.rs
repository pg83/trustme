// Extracted from library/core/src/cell.rs:2317
#![allow(unused)]
#![feature(unsafe_cell_access)]
fn main() {
    use std::cell::UnsafeCell;

    let uc = UnsafeCell::new(5);

    unsafe { *uc.as_mut_unchecked() += 1; }
    assert_eq!(uc.into_inner(), 6);
}
