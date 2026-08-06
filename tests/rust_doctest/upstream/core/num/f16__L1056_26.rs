// Extracted from library/core/src/num/f16.rs:1056
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let value = f16::from_le_bytes([0x40, 0x4a]);
    assert_eq!(value, 12.5);
    }
}
