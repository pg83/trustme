// Extracted from library/core/src/num/f128.rs:1237
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `{eq,gt,unord}tf` are available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    assert!((-3.0f128).clamp(-2.0, 1.0) == -2.0);
    assert!((0.0f128).clamp(-2.0, 1.0) == 0.0);
    assert!((2.0f128).clamp(-2.0, 1.0) == 1.0);
    assert!((f128::NAN).clamp(-2.0, 1.0).is_nan());
    }
}
