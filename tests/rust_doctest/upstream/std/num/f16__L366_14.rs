// Extracted from library/std/src/num/f16.rs:366
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 2.0 * std::f16::consts::PI;
    
    let abs_difference = (x.cos() - 1.0).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
