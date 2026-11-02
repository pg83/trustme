//@ compile-fail: tuple field index is too large in offset_of
#![feature(builtin_syntax)]
macro_rules! field_offset { ($index:expr) => { builtin # offset_of((u8, u16), $index) }; }
fn main() { let _ = field_offset!(18446744073709551616.25); }
