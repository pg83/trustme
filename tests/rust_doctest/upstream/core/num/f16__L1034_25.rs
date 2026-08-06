// Extracted from library/core/src/num/f16.rs:1034
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let value = f16::from_be_bytes([0x4a, 0x40]);
    assert_eq!(value, 12.5);
    }
}
