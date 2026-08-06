// Extracted from library/core/src/num/f16.rs:1803
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 8.0f16;
    
    // x^(1/3) - 2 == 0
    let abs_difference = (x.cbrt() - 2.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
