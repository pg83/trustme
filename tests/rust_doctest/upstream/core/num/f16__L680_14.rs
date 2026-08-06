// Extracted from library/core/src/num/f16.rs:680
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(target_arch = "aarch64")] { // FIXME(f16_F128): rust-lang/rust#123885
    
    let x = 1.0f16;
    let y = 2.0f16;
    
    assert_eq!(x.max(y), y);
    }
}
