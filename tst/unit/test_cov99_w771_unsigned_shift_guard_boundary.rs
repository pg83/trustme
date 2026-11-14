//@ compile-flags: -C overflow-checks=on
//@ run-fail: attempt to shift right with overflow
#![allow(arithmetic_overflow)]

fn main() {
    assert_eq!(1u128 >> 128u32, 1);
}
