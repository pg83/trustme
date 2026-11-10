#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn ub_checks() -> bool;
}

fn main() {
    assert!(unsafe { ub_checks() });
}
