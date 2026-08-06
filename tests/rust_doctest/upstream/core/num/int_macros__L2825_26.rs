// Extracted from library/core/src/num/int_macros.rs:2825
#![allow(unused)]
fn main() {
    assert_eq!(3i8.overflowing_pow(5), (-13, true));
}
