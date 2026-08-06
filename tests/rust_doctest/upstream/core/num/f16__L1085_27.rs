// Extracted from library/core/src/num/f16.rs:1085
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let value = f16::from_ne_bytes(if cfg!(target_endian = "big") {
        [0x4a, 0x40]
    } else {
        [0x40, 0x4a]
    });
    assert_eq!(value, 12.5);
    }
}
