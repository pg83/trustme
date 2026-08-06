// Extracted from library/std/src/num/f16.rs:620
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 1e-4_f16;
    
    // for very small x, e^x is approximately 1 + x + x^2 / 2
    let approx = x + x * x / 2.0;
    let abs_difference = (x.exp_m1() - approx).abs();
    
    assert!(abs_difference < 1e-4);
    }
}
