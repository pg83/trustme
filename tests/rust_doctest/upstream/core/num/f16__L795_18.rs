// Extracted from library/core/src/num/f16.rs:795
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(target_arch = "aarch64")] { // FIXME(f16_F128): rust-lang/rust#123885

    assert_eq!(1f16.midpoint(4.0), 2.5);
    assert_eq!((-5.5f16).midpoint(8.0), 1.25);
    }
}
