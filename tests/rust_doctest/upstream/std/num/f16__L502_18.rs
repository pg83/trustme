// Extracted from library/std/src/num/f16.rs:502
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 1.0f16;
    
    // atan(tan(1))
    let abs_difference = (f.tan().atan() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
