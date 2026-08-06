// Extracted from library/core/src/num/int_macros.rs:2519
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    assert_eq!(5i32.widening_mul(-2), (4294967286, -1));
    assert_eq!(1_000_000_000i32.widening_mul(-10), (2884901888, -3));
}
