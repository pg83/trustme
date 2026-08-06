// Extracted from library/core/src/num/f128.rs:1791
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let positive = 4.0_f128;
    let negative = -4.0_f128;
    let negative_zero = -0.0_f128;
    
    assert_eq!(positive.sqrt(), 2.0);
    assert!(negative.sqrt().is_nan());
    assert!(negative_zero.sqrt() == negative_zero);
    }
}
