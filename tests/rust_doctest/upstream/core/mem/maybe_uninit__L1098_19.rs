// Extracted from library/core/src/mem/maybe_uninit.rs:1098
#![allow(unused)]
#![feature(maybe_uninit_write_slice)]
fn main() {
    use std::mem::MaybeUninit;

    let mut dst = [const { MaybeUninit::uninit() }; 5];
    let src = ["wibbly", "wobbly", "timey", "wimey", "stuff"].map(|s| s.to_string());

    let init = dst.write_clone_of_slice(&src);

    assert_eq!(init, src);

    // Prevent leaks for Miri
    unsafe { std::ptr::drop_in_place(init); }
}
