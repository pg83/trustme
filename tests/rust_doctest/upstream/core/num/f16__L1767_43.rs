// Extracted from library/core/src/num/f16.rs:1767
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let positive = 4.0_f16;
    let negative = -4.0_f16;
    let negative_zero = -0.0_f16;
    
    assert_eq!(positive.sqrt(), 2.0);
    assert!(negative.sqrt().is_nan());
    assert!(negative_zero.sqrt() == negative_zero);
    }
}
