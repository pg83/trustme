// Extracted from library/core/src/num/f128.rs:356
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let min = f128::MIN_POSITIVE; // 3.362103143e-4932f128
    let max = f128::MAX;
    let lower_than_min = 1.0e-4960_f128;
    let zero = 0.0_f128;
    
    assert!(!min.is_subnormal());
    assert!(!max.is_subnormal());
    
    assert!(!zero.is_subnormal());
    assert!(!f128::NAN.is_subnormal());
    assert!(!f128::INFINITY.is_subnormal());
    // Values between `0` and `min` are Subnormal.
    assert!(lower_than_min.is_subnormal());
    }
}
