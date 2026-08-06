// Extracted from library/core/src/num/f128.rs:1720
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let a: f128 = 7.0;
    let b = 4.0;
    assert_eq!(a.rem_euclid(b), 3.0);
    assert_eq!((-a).rem_euclid(b), 1.0);
    assert_eq!(a.rem_euclid(-b), 3.0);
    assert_eq!((-a).rem_euclid(-b), 1.0);
    // limitation due to round-off error
    assert!((-f128::EPSILON).rem_euclid(3.0) != 0.0);
    }
}
