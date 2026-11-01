#![allow(arithmetic_overflow)]
#[inline(never)]
fn fail() -> u8 { let value=3u8;let shift=129u16;value<<shift }
fn main() { assert!(std::panic::catch_unwind(fail).is_err()); }
