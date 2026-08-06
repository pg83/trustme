// Extracted from library/core/src/num/f16.rs:1696
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let a: f16 = 7.0;
    let b = 4.0;
    assert_eq!(a.rem_euclid(b), 3.0);
    assert_eq!((-a).rem_euclid(b), 1.0);
    assert_eq!(a.rem_euclid(-b), 3.0);
    assert_eq!((-a).rem_euclid(-b), 1.0);
    // limitation due to round-off error
    assert!((-f16::EPSILON).rem_euclid(3.0) != 0.0);
    }
}
