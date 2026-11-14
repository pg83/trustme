//@ crate-type: lib
//@ compile-flags: -C overflow-checks=yes
#![allow(arithmetic_overflow)]

pub fn signed_shift_by_width() -> i128 {
    7_i128 >> 128_u32
}
