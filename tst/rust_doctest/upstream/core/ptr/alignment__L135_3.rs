// Extracted from library/core/src/ptr/alignment.rs:135
#![allow(unused)]
#![feature(ptr_alignment_type)]
#![feature(ptr_mask)]
fn main() {
    use std::ptr::{Alignment, NonNull};

    #[repr(align(1))] struct Align1(u8);
    #[repr(align(2))] struct Align2(u16);
    #[repr(align(4))] struct Align4(u32);
    let one = <NonNull<Align1>>::dangling().as_ptr();
    let two = <NonNull<Align2>>::dangling().as_ptr();
    let four = <NonNull<Align4>>::dangling().as_ptr();

    assert_eq!(four.mask(Alignment::of::<Align1>().mask()), four);
    assert_eq!(four.mask(Alignment::of::<Align2>().mask()), four);
    assert_eq!(four.mask(Alignment::of::<Align4>().mask()), four);
    assert_ne!(one.mask(Alignment::of::<Align4>().mask()), one);
}
