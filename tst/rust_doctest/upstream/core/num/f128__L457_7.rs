// Extracted from library/core/src/num/f128.rs:457
#![allow(unused)]
#![feature(f128)]
fn main() {

    let f = 7.0_f128;
    let g = -7.0_f128;

    assert!(f.is_sign_positive());
    assert!(!g.is_sign_positive());
}
