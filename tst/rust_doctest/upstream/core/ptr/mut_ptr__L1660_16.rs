// Extracted from library/core/src/ptr/mut_ptr.rs:1660
#![allow(unused)]
#![feature(pointer_is_aligned_to)]
fn main() {

    // On some platforms, the alignment of i32 is less than 4.
    #[repr(align(4))]
    struct AlignedI32(i32);

    let mut data = AlignedI32(42);
    let ptr = &mut data as *mut AlignedI32;

    assert!(ptr.is_aligned_to(1));
    assert!(ptr.is_aligned_to(2));
    assert!(ptr.is_aligned_to(4));

    assert!(ptr.wrapping_byte_add(2).is_aligned_to(2));
    assert!(!ptr.wrapping_byte_add(2).is_aligned_to(4));

    assert_ne!(ptr.is_aligned_to(8), ptr.wrapping_add(1).is_aligned_to(8));
}
