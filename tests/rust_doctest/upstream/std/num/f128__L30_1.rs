// Extracted from library/std/src/num/f128.rs:30
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 2.0_f128;
    let abs_difference = (x.powf(2.0) - (x * x)).abs();
    assert!(abs_difference <= f128::EPSILON);
    
    assert_eq!(f128::powf(1.0, f128::NAN), 1.0);
    assert_eq!(f128::powf(f128::NAN, 0.0), 1.0);
    }
}
