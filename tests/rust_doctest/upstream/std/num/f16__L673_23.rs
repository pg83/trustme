// Extracted from library/std/src/num/f16.rs:673
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    assert_eq!((-1.0_f16).ln_1p(), f16::NEG_INFINITY);
    assert!((-2.0_f16).ln_1p().is_nan());
    }
}
