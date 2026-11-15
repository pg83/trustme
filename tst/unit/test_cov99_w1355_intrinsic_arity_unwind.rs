//@ compile-only
//@ compile-fail: vector
#![feature(intrinsics)]

extern "rust-intrinsic" {
    fn wrapping_add<T>(value: T) -> T;
}

const RESULT: u8 = unsafe { wrapping_add::<u8>(7) };

fn main() {}
