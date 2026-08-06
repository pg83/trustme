// Extracted from library/std/src/num/f128.rs:981
#![allow(unused)]
#![feature(f128)]
#![feature(float_gamma)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 2.0f128;
    
    let abs_difference = (x.ln_gamma().0 - 0.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
