//@ crate-type: lib
//@ compile-fail: rotate_left
#![feature(core_intrinsics)]

pub fn rotate_bool(value: bool, shift: u32) -> bool {
    unsafe { core::intrinsics::rotate_left::<bool>(value, shift) }
}
