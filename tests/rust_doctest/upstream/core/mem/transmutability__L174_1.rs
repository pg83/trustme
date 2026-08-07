// Extracted from library/core/src/mem/transmutability.rs:174
#![allow(unused)]
#![feature(pointer_is_aligned_to, transmutability)]
fn main() {
    use core::mem::{Assume, TransmuteFrom};

    let src: &[u8; 2] = &[0xFF, 0xFF];

    let maybe_dst: Option<&u16> = if <*const _>::is_aligned_to(src, align_of::<u16>()) {
        // SAFETY: We have checked above that the address of `src` satisfies the
        // alignment requirements of `u16`.
        Some(unsafe {
            <_ as TransmuteFrom<_, { Assume::ALIGNMENT }>>::transmute(src)
        })
    } else {
        None
    };

    assert!(matches!(maybe_dst, Some(&u16::MAX) | None));
}
