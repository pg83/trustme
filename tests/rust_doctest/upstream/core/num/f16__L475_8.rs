// Extracted from library/core/src/num/f16.rs:475
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): LLVM crashes on s390x, llvm/llvm-project#50374
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let f = 7.0_f16;
    let g = -7.0_f16;
    
    assert!(!f.is_sign_negative());
    assert!(g.is_sign_negative());
    }
}
