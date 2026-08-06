// Extracted from library/std/src/num/f16.rs:703
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let e = std::f16::consts::E;
    let x = 1.0f16;
    
    let f = x.sinh();
    // Solving sinh() at 1 gives `(e^2-1)/(2e)`
    let g = ((e * e) - 1.0) / (2.0 * e);
    let abs_difference = (f - g).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
