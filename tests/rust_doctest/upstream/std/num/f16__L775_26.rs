// Extracted from library/std/src/num/f16.rs:775
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let e = std::f16::consts::E;
    let x = 1.0f16;
    
    let f = x.tanh();
    // Solving tanh() at 1 gives `(1 - e^(-2))/(1 + e^(-2))`
    let g = (1.0 - e.powi(-2)) / (1.0 + e.powi(-2));
    let abs_difference = (f - g).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
