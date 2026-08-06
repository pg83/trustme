// Extracted from library/std/src/num/f128.rs:124
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let one = 1.0f128;
    // e^1
    let e = one.exp();
    
    // ln(e) - 1 == 0
    let abs_difference = (e.ln() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
