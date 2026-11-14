//@ crate-type: lib
//@ compile-fail: used binding isn't initialized
//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir -C panic=abort
#![feature(core_intrinsics)]

pub unsafe fn missing_index(values: *const [u8]) -> *const u8 {
    let index: usize;
    core::intrinsics::slice_get_unchecked(values, index)
}
