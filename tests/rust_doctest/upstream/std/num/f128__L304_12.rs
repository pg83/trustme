// Extracted from library/std/src/num/f128.rs:304
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 8.0f128;
    
    // x^(1/3) - 2 == 0
    let abs_difference = (x.cbrt() - 2.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
