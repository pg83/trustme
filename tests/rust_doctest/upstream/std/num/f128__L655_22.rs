// Extracted from library/std/src/num/f128.rs:655
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 1e-8_f128;
    
    // for very small x, e^x is approximately 1 + x + x^2 / 2
    let approx = x + x * x / 2.0;
    let abs_difference = (x.exp_m1() - approx).abs();
    
    assert!(abs_difference < 1e-10);
    }
}
