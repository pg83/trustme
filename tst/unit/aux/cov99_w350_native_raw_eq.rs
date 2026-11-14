#![feature(core_intrinsics)]
#![allow(internal_features)]

pub const fn equal(left: u32, right: u32) -> bool {
    unsafe { core::intrinsics::raw_eq(&left, &right) }
}
