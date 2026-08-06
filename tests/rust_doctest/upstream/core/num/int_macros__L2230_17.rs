// Extracted from library/core/src/num/int_macros.rs:2230
#![allow(unused)]
fn main() {
    assert_eq!(3i8.wrapping_pow(5), -13);
    assert_eq!(3i8.wrapping_pow(6), -39);
}
