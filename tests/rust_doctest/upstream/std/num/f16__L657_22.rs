// Extracted from library/std/src/num/f16.rs:657
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 1e-4_f16;
    
    // for very small x, ln(1 + x) is approximately x - x^2 / 2
    let approx = x - x * x / 2.0;
    let abs_difference = (x.ln_1p() - approx).abs();
    
    assert!(abs_difference < 1e-4);
    }
}
