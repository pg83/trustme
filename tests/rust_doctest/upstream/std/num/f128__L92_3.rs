// Extracted from library/std/src/num/f128.rs:92
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 2.0f128;
    
    // 2^2 - 4 == 0
    let abs_difference = (f.exp2() - 4.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
