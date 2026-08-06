// Extracted from library/std/src/num/f128.rs:216
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let two = 2.0f128;
    
    // log2(2) - 1 == 0
    let abs_difference = (two.log2() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
