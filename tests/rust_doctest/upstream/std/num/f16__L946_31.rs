// Extracted from library/std/src/num/f16.rs:946
#![allow(unused)]
#![feature(f16)]
#![feature(float_gamma)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 2.0f16;
    
    let abs_difference = (x.ln_gamma().0 - 0.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
