// Extracted from library/std/src/num/f128.rs:708
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    assert_eq!((-1.0_f128).ln_1p(), f128::NEG_INFINITY);
    assert!((-2.0_f128).ln_1p().is_nan());
    }
}
