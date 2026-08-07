// Extracted from library/core/src/num/f128.rs:770
#![allow(unused)]
#![feature(f128)]
#![feature(float_minimum_maximum)]
fn main() {
    // Using aarch64 because `reliable_f128_math` is needed
    #[cfg(all(target_arch = "aarch64", target_os = "linux"))] {

    let x = 1.0f128;
    let y = 2.0f128;

    assert_eq!(x.minimum(y), x);
    assert!(x.minimum(f128::NAN).is_nan());
    }
}
