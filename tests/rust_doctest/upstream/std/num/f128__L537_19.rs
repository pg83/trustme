// Extracted from library/std/src/num/f128.rs:537
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 1.0f128;
    
    // atan(tan(1))
    let abs_difference = (f.tan().atan() - 1.0).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
