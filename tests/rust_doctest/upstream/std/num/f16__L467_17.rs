// Extracted from library/std/src/num/f16.rs:467
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = std::f16::consts::FRAC_PI_4;
    
    // acos(cos(pi/4))
    let abs_difference = (f.cos().acos() - std::f16::consts::FRAC_PI_4).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
