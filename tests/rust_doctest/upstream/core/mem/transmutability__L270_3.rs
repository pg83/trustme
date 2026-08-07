// Extracted from library/core/src/mem/transmutability.rs:270
#![allow(unused)]
#![feature(transmutability)]
fn main() {
    use core::mem::{Assume, TransmuteFrom};

    let src: u8 = 1;

    let maybe_dst: Option<bool> = if src == 0 || src == 1 {
        // SAFETY: We have checked above that the value of `src` is a bit-valid
        // instance of `bool`.
        Some(unsafe {
            <_ as TransmuteFrom<_, { Assume::VALIDITY }>>::transmute(src)
        })
    } else {
        None
    };

    assert_eq!(maybe_dst, Some(true));
}
