// Extracted from library/core/src/num/f16.rs:876
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    // FIXME(f16_f128): enable this once const casting works
    // assert_ne!((1f16).to_bits(), 1f16 as u128); // to_bits() is not casting!
    assert_eq!((12.5f16).to_bits(), 0x4a40);
    }
}
