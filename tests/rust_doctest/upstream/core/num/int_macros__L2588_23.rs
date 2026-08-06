// Extracted from library/core/src/num/int_macros.rs:2588
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    assert_eq!(5i32.carrying_mul_add(-2, 0, 0), (4294967286, -1));
    assert_eq!(5i32.carrying_mul_add(-2, 10, 10), (10, 0));
    assert_eq!(1_000_000_000i32.carrying_mul_add(-10, 0, 0), (2884901888, -3));
    assert_eq!(1_000_000_000i32.carrying_mul_add(-10, 10, 10), (2884901908, -3));
}
