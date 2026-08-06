// Extracted from library/std/src/num/f128.rs:945
#![allow(unused)]
#![feature(f128)]
#![feature(float_gamma)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 5.0f128;
    
    let abs_difference = (x.gamma() - 24.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
