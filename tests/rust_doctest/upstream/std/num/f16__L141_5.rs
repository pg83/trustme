// Extracted from library/std/src/num/f16.rs:141
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    assert_eq!(0_f16.ln(), f16::NEG_INFINITY);
    assert!((-42_f16).ln().is_nan());
    }
}
