// Extracted from library/core/src/num/f16.rs:761
#![allow(unused)]
#![feature(f16)]
#![feature(float_minimum_maximum)]
fn main() {
    #[cfg(target_arch = "aarch64")] { // FIXME(f16_F128): rust-lang/rust#123885
    
    let x = 1.0f16;
    let y = 2.0f16;
    
    assert_eq!(x.minimum(y), x);
    assert!(x.minimum(f16::NAN).is_nan());
    }
}
