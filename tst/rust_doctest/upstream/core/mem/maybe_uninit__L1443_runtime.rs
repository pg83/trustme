// Extracted from library/core/src/mem/maybe_uninit.rs:1443
#![allow(unused)]
#![feature(maybe_uninit_uninit_array_transpose)]
fn main() {
    use std::mem::MaybeUninit;

    let data: [MaybeUninit<u8>; 1000] = MaybeUninit::uninit().transpose();
}
