// Extracted from library/std/src/num/f16.rs:60
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let one = 1.0f16;
    // e^1
    let e = one.exp();
    
    // ln(e) - 1 == 0
    let abs_difference = (e.ln() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
