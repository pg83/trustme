// Extracted from library/core/src/num/f128.rs:805
#![allow(unused)]
#![feature(f128)]
fn main() {
    // Using aarch64 because `reliable_f128_math` is needed
    #[cfg(all(target_arch = "aarch64", target_os = "linux"))] {

    assert_eq!(1f128.midpoint(4.0), 2.5);
    assert_eq!((-5.5f128).midpoint(8.0), 1.25);
    }
}
