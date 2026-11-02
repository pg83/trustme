//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
macro_rules! first_only {
    ($value:expr) => {{ let first = $value; #[cfg(any())] let _ = $value; first }};
}
fn main() { let _ = first_only!(builtin # type_ascribe(|| 113u16, fn() -> u16)); }
