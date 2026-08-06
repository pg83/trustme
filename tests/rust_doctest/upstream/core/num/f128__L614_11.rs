// Extracted from library/core/src/num/f128.rs:614
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let x = 2.0_f128;
    let abs_difference = (x.recip() - (1.0 / x)).abs();
    
    assert!(abs_difference <= f128::EPSILON);
    }
}
