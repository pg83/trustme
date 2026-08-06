// Extracted from library/core/src/num/f16.rs:1217
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    assert!((-3.0f16).clamp(-2.0, 1.0) == -2.0);
    assert!((0.0f16).clamp(-2.0, 1.0) == 0.0);
    assert!((2.0f16).clamp(-2.0, 1.0) == 1.0);
    assert!((f16::NAN).clamp(-2.0, 1.0).is_nan());
    }
}
