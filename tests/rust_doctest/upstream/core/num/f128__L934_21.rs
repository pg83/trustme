// Extracted from library/core/src/num/f128.rs:934
#![allow(unused)]
#![feature(f128)]
fn main() {
     // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let v = f128::from_bits(0x40029000000000000000000000000000);
    assert_eq!(v, 12.5);
    }
}
