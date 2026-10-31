#![feature(core_intrinsics)]
const TRUE: bool = std::intrinsics::unlikely(true);
const FALSE: bool = std::intrinsics::unlikely(false);
fn main() { assert!(TRUE); assert!(!FALSE); }
