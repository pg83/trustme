// Extracted from library/core/src/num/f16.rs:974
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): LLVM crashes on s390x, llvm/llvm-project#50374
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let bytes = 12.5f16.to_le_bytes();
    assert_eq!(bytes, [0x40, 0x4a]);
    }
}
