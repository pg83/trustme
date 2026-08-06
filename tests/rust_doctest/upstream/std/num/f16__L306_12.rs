// Extracted from library/std/src/num/f16.rs:306
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 2.0f16;
    let y = 3.0f16;
    
    // sqrt(x^2 + y^2)
    let abs_difference = (x.hypot(y) - (x.powi(2) + y.powi(2)).sqrt()).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
