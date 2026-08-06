// Extracted from library/std/src/num/f128.rs:274
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    assert_eq!(0_f128.log10(), f128::NEG_INFINITY);
    assert!((-42_f128).log10().is_nan());
    }
}
