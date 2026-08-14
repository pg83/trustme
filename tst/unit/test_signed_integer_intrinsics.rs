#![feature(core_intrinsics)]

use std::intrinsics::{ctlz, ctlz_nonzero, ctpop, cttz, cttz_nonzero};

fn main() {
    unsafe {
        assert_eq!(cttz_nonzero(8i8), 3);
        assert_eq!(ctlz_nonzero(1i8), 7);
    }
    assert_eq!(ctlz(-1i8), 0);
    assert_eq!(ctpop(-1i8), 8);
    assert_eq!(ctpop(-1i16), 16);
    assert_eq!(ctpop(-1i32), 32);
    assert_eq!(ctpop(-1i64), 64);
    assert_eq!(ctpop(-1i128), 128);
    assert_eq!(ctpop(-1isize), isize::BITS);
    assert_eq!(ctlz(1i8), 7);
    assert_eq!(cttz(8i8), 3);
}
