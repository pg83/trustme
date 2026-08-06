// Extracted from library/core/src/num/f128.rs:327
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `lttf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let f = 7.0f128;
    let inf: f128 = f128::INFINITY;
    let neg_inf: f128 = f128::NEG_INFINITY;
    let nan: f128 = f128::NAN;
    
    assert!(f.is_finite());
    
    assert!(!nan.is_finite());
    assert!(!inf.is_finite());
    assert!(!neg_inf.is_finite());
    }
}
