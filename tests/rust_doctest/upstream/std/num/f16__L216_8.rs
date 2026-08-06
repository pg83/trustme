// Extracted from library/std/src/num/f16.rs:216
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let two = 2.0f16;
    
    // log2(2) - 1 == 0
    let abs_difference = (two.log2() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
