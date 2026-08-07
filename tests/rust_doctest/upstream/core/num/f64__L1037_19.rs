// Extracted from library/core/src/num/f64.rs:1037
#![allow(unused)]
fn main() {
    let value = 4.6_f64;
    let rounded = unsafe { value.to_int_unchecked::<u16>() };
    assert_eq!(rounded, 4);

    let value = -128.9_f64;
    let rounded = unsafe { value.to_int_unchecked::<i8>() };
    assert_eq!(rounded, i8::MIN);
}
