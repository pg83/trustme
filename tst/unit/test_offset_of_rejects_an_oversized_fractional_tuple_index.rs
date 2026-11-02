//@ compile-fail: invalid tuple field index in offset_of
#![feature(builtin_syntax)]
macro_rules! field_offset { ($index:expr) => { builtin # offset_of((u8, u16), $index) }; }
fn main() { let _ = field_offset!(0.98765432109876543219); }
