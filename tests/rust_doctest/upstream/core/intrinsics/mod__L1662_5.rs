// Extracted from library/core/src/intrinsics/mod.rs:1662
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {
    
    use std::intrinsics::ctlz;
    
    let x = 0b0001_1100_u8;
    let num_leading = ctlz(x);
    assert_eq!(num_leading, 3);
}
