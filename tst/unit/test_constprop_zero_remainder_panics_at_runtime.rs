#![allow(unconditional_panic)]
#[inline(never)]
fn fail() -> u32 { let numerator=17u32;let denominator=0u32;numerator%denominator }
fn main() { assert!(std::panic::catch_unwind(fail).is_err()); }
