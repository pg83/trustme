// Extracted from library/core/src/intrinsics/mod.rs:1697
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {
    
    use std::intrinsics::ctlz_nonzero;
    
    let x = 0b0001_1100_u8;
    let num_leading = unsafe { ctlz_nonzero(x) };
    assert_eq!(num_leading, 3);
}
