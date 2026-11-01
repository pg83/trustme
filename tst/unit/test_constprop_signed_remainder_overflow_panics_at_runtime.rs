#![allow(unconditional_panic)]
#[inline(never)]
fn fail() -> i64 { let value=i64::MIN;let divisor=-1i64;value%divisor }
fn main() { assert!(std::panic::catch_unwind(fail).is_err()); }
