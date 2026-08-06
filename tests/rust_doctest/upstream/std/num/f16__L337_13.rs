// Extracted from library/std/src/num/f16.rs:337
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = std::f16::consts::FRAC_PI_2;
    
    let abs_difference = (x.sin() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
