// Extracted from library/core/src/num/f16.rs:379
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let min = f16::MIN_POSITIVE; // 6.1035e-5
    let max = f16::MAX;
    let lower_than_min = 1.0e-7_f16;
    let zero = 0.0_f16;

    assert!(min.is_normal());
    assert!(max.is_normal());

    assert!(!zero.is_normal());
    assert!(!f16::NAN.is_normal());
    assert!(!f16::INFINITY.is_normal());
    // Values between `0` and `min` are Subnormal.
    assert!(!lower_than_min.is_normal());
    }
}
