// Extracted from library/core/src/num/f128.rs:1677
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let a: f128 = 7.0;
    let b = 4.0;
    assert_eq!(a.div_euclid(b), 1.0); // 7.0 > 4.0 * 1.0
    assert_eq!((-a).div_euclid(b), -2.0); // -7.0 >= 4.0 * -2.0
    assert_eq!(a.div_euclid(-b), -1.0); // 7.0 >= -4.0 * -1.0
    assert_eq!((-a).div_euclid(-b), 2.0); // -7.0 >= -4.0 * 2.0
    }
}
