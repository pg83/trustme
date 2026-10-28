//@ compile-fail: requires a valid power-of-two alignment
#![feature(core_intrinsics, const_heap)]
const POINTER: *mut u8 = unsafe { core::intrinsics::const_allocate(4, 3) };
fn main() { let _ = POINTER; }
