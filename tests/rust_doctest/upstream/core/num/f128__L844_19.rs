// Extracted from library/core/src/num/f128.rs:844
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `float*itf` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let value = 4.6_f128;
    let rounded = unsafe { value.to_int_unchecked::<u16>() };
    assert_eq!(rounded, 4);
    
    let value = -128.9_f128;
    let rounded = unsafe { value.to_int_unchecked::<i8>() };
    assert_eq!(rounded, i8::MIN);
    }
}
