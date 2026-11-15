//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(repr_simd)]

#[repr(simd)]
pub struct ThreeWords([u32; 3]);

pub const THREE_WORD_SIZE: usize = core::mem::size_of::<ThreeWords>();
pub const THREE_WORD_ALIGNMENT: usize = core::mem::align_of::<ThreeWords>();
