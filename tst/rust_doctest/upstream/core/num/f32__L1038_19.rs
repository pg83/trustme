// Extracted from library/core/src/num/f32.rs:1038
#![allow(unused)]
fn main() {
    let value = 4.6_f32;
    let rounded = unsafe { value.to_int_unchecked::<u16>() };
    assert_eq!(rounded, 4);

    let value = -128.9_f32;
    let rounded = unsafe { value.to_int_unchecked::<i8>() };
    assert_eq!(rounded, i8::MIN);
}
