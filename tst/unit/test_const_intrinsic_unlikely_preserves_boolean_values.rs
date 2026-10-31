#![feature(core_intrinsics)]
const YES: bool = std::intrinsics::unlikely(true);
const NO: bool = std::intrinsics::unlikely(false);
fn main() { assert!(YES); assert!(!NO); }
