// Extracted from library/core/src/num/f128.rs:1758
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 2.0_f128;
    let abs_difference = (x.powi(2) - (x * x)).abs();
    assert!(abs_difference <= f128::EPSILON);
    
    assert_eq!(f128::powi(f128::NAN, 0), 1.0);
    }
}
