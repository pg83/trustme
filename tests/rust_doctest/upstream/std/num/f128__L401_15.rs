// Extracted from library/std/src/num/f128.rs:401
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 2.0 * std::f128::consts::PI;
    
    let abs_difference = (x.cos() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
