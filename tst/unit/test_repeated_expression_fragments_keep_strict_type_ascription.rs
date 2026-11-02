//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
macro_rules! repeat { ($value:expr) => { ($value, $value) }; }
fn main() { let _ = repeat!(builtin # type_ascribe(|| 53u16, fn() -> u16)); }
