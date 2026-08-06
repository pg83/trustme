// Extracted from library/std/src/num/f128.rs:774
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let e = std::f128::consts::E;
    let x = 1.0f128;
    let f = x.cosh();
    // Solving cosh() at 1 gives this result
    let g = ((e * e) + 1.0) / (2.0 * e);
    let abs_difference = (f - g).abs();
    
    // Same result
    assert!(abs_difference <= f128::EPSILON);
    }
}
