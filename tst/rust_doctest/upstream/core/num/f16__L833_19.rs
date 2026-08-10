// Extracted from library/core/src/num/f16.rs:833
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let value = 4.6_f16;
    let rounded = unsafe { value.to_int_unchecked::<u16>() };
    assert_eq!(rounded, 4);

    let value = -128.9_f16;
    let rounded = unsafe { value.to_int_unchecked::<i8>() };
    assert_eq!(rounded, i8::MIN);
    }
}
