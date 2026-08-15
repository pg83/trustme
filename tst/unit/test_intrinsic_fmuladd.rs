#![feature(core_intrinsics)]
#![allow(internal_features)]

fn main() {
    unsafe {
        assert_eq!(core::intrinsics::fmuladdf32(1.5, 2.0, 0.25), 3.25);
        assert_eq!(core::intrinsics::fmuladdf64(-2.0, 4.0, 1.0), -7.0);
    }
}
