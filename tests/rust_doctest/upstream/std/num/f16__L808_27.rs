// Extracted from library/std/src/num/f16.rs:808
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 1.0f16;
    let f = x.sinh().asinh();
    
    let abs_difference = (f - x).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
