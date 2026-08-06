// Extracted from library/std/src/num/f16.rs:876
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let e = std::f16::consts::E;
    let f = e.tanh().atanh();
    
    let abs_difference = (f - e).abs();
    
    assert!(abs_difference <= 0.01);
    }
}
