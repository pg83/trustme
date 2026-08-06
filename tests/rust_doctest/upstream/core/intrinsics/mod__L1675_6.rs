// Extracted from library/core/src/intrinsics/mod.rs:1675
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {
    
    use std::intrinsics::ctlz;
    
    let x = 0u16;
    let num_leading = ctlz(x);
    assert_eq!(num_leading, 16);
}
