// Extracted from library/std/src/num/f16.rs:431
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = std::f16::consts::FRAC_PI_2;
    
    // asin(sin(pi/2))
    let abs_difference = (f.sin().asin() - std::f16::consts::FRAC_PI_2).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
