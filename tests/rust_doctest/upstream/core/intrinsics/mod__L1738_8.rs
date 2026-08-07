// Extracted from library/core/src/intrinsics/mod.rs:1738
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {

    use std::intrinsics::cttz;

    let x = 0u16;
    let num_trailing = cttz(x);
    assert_eq!(num_trailing, 16);
}
