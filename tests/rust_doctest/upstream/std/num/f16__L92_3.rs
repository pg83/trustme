// Extracted from library/std/src/num/f16.rs:92
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 2.0f16;
    
    // 2^2 - 4 == 0
    let abs_difference = (f.exp2() - 4.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
