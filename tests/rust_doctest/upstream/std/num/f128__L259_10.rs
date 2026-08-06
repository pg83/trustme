// Extracted from library/std/src/num/f128.rs:259
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let ten = 10.0f128;
    
    // log10(10) - 1 == 0
    let abs_difference = (ten.log10() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
