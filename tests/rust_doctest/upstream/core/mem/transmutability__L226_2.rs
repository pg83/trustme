// Extracted from library/core/src/mem/transmutability.rs:226
#![allow(unused)]
#![feature(transmutability)]
fn main() {
    use core::mem::{Assume, TransmuteFrom};

    let src: u8 = 42;

    struct EvenU8 {
        // SAFETY: `val` must be an even number.
        val: u8,
    }

    let maybe_dst: Option<EvenU8> = if src % 2 == 0 {
        // SAFETY: We have checked above that the value of `src` is even.
        Some(unsafe {
            <_ as TransmuteFrom<_, { Assume::SAFETY }>>::transmute(src)
        })
    } else {
        None
    };

    assert!(matches!(maybe_dst, Some(EvenU8 { val: 42 })));
}
