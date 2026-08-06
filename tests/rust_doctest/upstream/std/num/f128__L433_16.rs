// Extracted from library/std/src/num/f128.rs:433
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = std::f128::consts::FRAC_PI_4;
    let abs_difference = (x.tan() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
