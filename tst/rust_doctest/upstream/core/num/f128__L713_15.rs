// Extracted from library/core/src/num/f128.rs:713
#![allow(unused)]
#![feature(f128)]
fn main() {
    // Using aarch64 because `reliable_f128_math` is needed
    #[cfg(all(target_arch = "aarch64", target_os = "linux"))] {

    let x = 1.0f128;
    let y = 2.0f128;

    assert_eq!(x.min(y), x);
    }
}
