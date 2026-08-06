// Extracted from library/std/src/num/f128.rs:1057
#![allow(unused)]
#![feature(f128)]
#![feature(float_erf)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    let x: f128 = 0.123;
    
    let one = x.erf() + x.erfc();
    let abs_difference = (one - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
