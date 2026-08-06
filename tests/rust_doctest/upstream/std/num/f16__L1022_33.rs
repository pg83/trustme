// Extracted from library/std/src/num/f16.rs:1022
#![allow(unused)]
#![feature(f16)]
#![feature(float_erf)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    let x: f16 = 0.123;
    
    let one = x.erf() + x.erfc();
    let abs_difference = (one - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
