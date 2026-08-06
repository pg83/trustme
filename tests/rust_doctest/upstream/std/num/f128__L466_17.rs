// Extracted from library/std/src/num/f128.rs:466
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = std::f128::consts::FRAC_PI_2;
    
    // asin(sin(pi/2))
    let abs_difference = (f.sin().asin() - std::f128::consts::FRAC_PI_2).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
