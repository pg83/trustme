//@ compile-flags: -Zmir-opt-level=3
#![feature(core_intrinsics)]
#![allow(internal_features)]
struct Empty;
struct Owned(String);
const fn owned_needs_drop() -> bool { std::intrinsics::needs_drop::<Owned>() }
const fn empty_needs_drop() -> bool { std::intrinsics::needs_drop::<Empty>() }
const ANSWERS: (bool, bool) = (owned_needs_drop(), empty_needs_drop());
fn main() { assert_eq!(ANSWERS, (true, false)); }
