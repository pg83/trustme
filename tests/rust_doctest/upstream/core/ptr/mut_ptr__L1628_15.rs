// Extracted from library/core/src/ptr/mut_ptr.rs:1628
#![allow(unused)]
fn main() {
    // On some platforms, the alignment of i32 is less than 4.
    #[repr(align(4))]
    struct AlignedI32(i32);

    let mut data = AlignedI32(42);
    let ptr = &mut data as *mut AlignedI32;

    assert!(ptr.is_aligned());
    assert!(!ptr.wrapping_byte_add(1).is_aligned());
}
