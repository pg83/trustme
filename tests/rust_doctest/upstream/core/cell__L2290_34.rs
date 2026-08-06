// Extracted from library/core/src/cell.rs:2290
#![allow(unused)]
#![feature(unsafe_cell_access)]
fn main() {
    use std::cell::UnsafeCell;
    
    let uc = UnsafeCell::new(5);
    
    let val = unsafe { uc.as_ref_unchecked() };
    assert_eq!(val, &5);
}
