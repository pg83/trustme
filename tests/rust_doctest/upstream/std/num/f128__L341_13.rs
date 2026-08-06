// Extracted from library/std/src/num/f128.rs:341
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 2.0f128;
    let y = 3.0f128;
    
    // sqrt(x^2 + y^2)
    let abs_difference = (x.hypot(y) - (x.powi(2) + y.powi(2)).sqrt()).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
