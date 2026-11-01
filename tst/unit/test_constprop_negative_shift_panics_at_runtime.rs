#![allow(arithmetic_overflow)]
#[inline(never)]
fn fail() -> i32 { let value=3i32;let shift=-1i8;value>>shift }
fn main() { assert!(std::panic::catch_unwind(fail).is_err()); }
