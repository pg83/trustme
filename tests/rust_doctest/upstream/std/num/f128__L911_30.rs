// Extracted from library/std/src/num/f128.rs:911
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let e = std::f128::consts::E;
    let f = e.tanh().atanh();
    
    let abs_difference = (f - e).abs();
    
    assert!(abs_difference <= 1e-5);
    }
}
