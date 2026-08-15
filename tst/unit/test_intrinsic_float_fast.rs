#![feature(core_intrinsics)]
#![allow(internal_features)]

fn main() {
    unsafe {
        assert_eq!(core::intrinsics::fadd_fast(7.0f64, 2.0), 9.0);
        assert_eq!(core::intrinsics::fsub_fast(7.0f64, 2.0), 5.0);
        assert_eq!(core::intrinsics::fmul_fast(7.0f64, 2.0), 14.0);
        assert_eq!(core::intrinsics::fdiv_fast(7.0f64, 2.0), 3.5);
        assert_eq!(core::intrinsics::frem_fast(7.0f64, 2.0), 1.0);
    }
}
