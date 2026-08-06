// Extracted from library/std/src/num/f16.rs:173
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let five = 5.0f16;
    
    // log5(5) - 1 == 0
    let abs_difference = (five.log(5.0) - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
