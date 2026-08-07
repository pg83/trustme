// Extracted from library/core/src/num/f16.rs:924
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let v = f16::from_bits(0x4a40);
    assert_eq!(v, 12.5);
    }
}
