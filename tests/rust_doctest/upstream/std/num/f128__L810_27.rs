// Extracted from library/std/src/num/f128.rs:810
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let e = std::f128::consts::E;
    let x = 1.0f128;
    
    let f = x.tanh();
    // Solving tanh() at 1 gives `(1 - e^(-2))/(1 + e^(-2))`
    let g = (1.0 - e.powi(-2)) / (1.0 + e.powi(-2));
    let abs_difference = (f - g).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
