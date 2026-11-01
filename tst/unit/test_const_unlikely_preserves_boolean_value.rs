#![feature(core_intrinsics)]
const fn hint(value: bool) -> bool { std::intrinsics::unlikely(value) }
const VALUE: bool = hint(true);
fn main() { assert!(VALUE); assert!(!hint(false)); }
