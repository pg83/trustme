// Extracted from library/std/src/num/f16.rs:910
#![allow(unused)]
#![feature(f16)]
#![feature(float_gamma)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 5.0f16;
    
    let abs_difference = (x.gamma() - 24.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
