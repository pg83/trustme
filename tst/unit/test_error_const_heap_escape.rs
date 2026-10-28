//@ compile-fail: a const heap allocation escaped without const_make_global
#![feature(core_intrinsics, const_heap)]
const POINTER: *mut u8 = unsafe { core::intrinsics::const_allocate(4, 4) };
fn main() { let _ = POINTER; }
