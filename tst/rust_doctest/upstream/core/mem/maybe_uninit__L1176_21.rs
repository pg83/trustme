// Extracted from library/core/src/mem/maybe_uninit.rs:1176
#![allow(unused)]
#![feature(maybe_uninit_fill)]
fn main() {
    use std::mem::MaybeUninit;

    let mut buf = [const { MaybeUninit::uninit() }; 10];
    let initialized = buf.write_filled(1);
    assert_eq!(initialized, &mut [1; 10]);
}
