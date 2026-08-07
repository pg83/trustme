// Extracted from library/core/src/intrinsics/mod.rs:1725
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {

    use std::intrinsics::cttz;

    let x = 0b0011_1000_u8;
    let num_trailing = cttz(x);
    assert_eq!(num_trailing, 3);
}
