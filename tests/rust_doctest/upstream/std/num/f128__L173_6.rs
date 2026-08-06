// Extracted from library/std/src/num/f128.rs:173
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let five = 5.0f128;
    
    // log5(5) - 1 == 0
    let abs_difference = (five.log(5.0) - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
