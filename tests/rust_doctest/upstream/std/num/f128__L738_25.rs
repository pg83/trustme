// Extracted from library/std/src/num/f128.rs:738
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let e = std::f128::consts::E;
    let x = 1.0f128;
    
    let f = x.sinh();
    // Solving sinh() at 1 gives `(e^2-1)/(2e)`
    let g = ((e * e) - 1.0) / (2.0 * e);
    let abs_difference = (f - g).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
