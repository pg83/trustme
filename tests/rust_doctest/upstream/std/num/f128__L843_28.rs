// Extracted from library/std/src/num/f128.rs:843
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 1.0f128;
    let f = x.sinh().asinh();
    
    let abs_difference = (f - x).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
