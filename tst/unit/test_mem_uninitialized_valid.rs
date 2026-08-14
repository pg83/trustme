#![allow(deprecated)]
#![feature(core_intrinsics)]

use std::mem::{self, MaybeUninit};

struct Zst;

const _: () = std::intrinsics::assert_mem_uninitialized_valid::<Zst>();

fn main() {
    unsafe {
        let _: Zst = mem::uninitialized();
        let _: MaybeUninit<i32> = mem::uninitialized();
    }
}
