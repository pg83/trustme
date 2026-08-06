// Extracted from library/std/src/num/f128.rs:502
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = std::f128::consts::FRAC_PI_4;
    
    // acos(cos(pi/4))
    let abs_difference = (f.cos().acos() - std::f128::consts::FRAC_PI_4).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
