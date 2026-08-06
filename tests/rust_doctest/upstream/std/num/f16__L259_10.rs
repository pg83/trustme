// Extracted from library/std/src/num/f16.rs:259
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let ten = 10.0f16;
    
    // log10(10) - 1 == 0
    let abs_difference = (ten.log10() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
