// Extracted from library/std/src/num/f16.rs:739
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let e = std::f16::consts::E;
    let x = 1.0f16;
    let f = x.cosh();
    // Solving cosh() at 1 gives this result
    let g = ((e * e) + 1.0) / (2.0 * e);
    let abs_difference = (f - g).abs();
    
    // Same result
    assert!(abs_difference <= f16::EPSILON);
    }
}
