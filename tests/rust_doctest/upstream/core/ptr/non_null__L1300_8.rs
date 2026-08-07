// Extracted from library/core/src/ptr/non_null.rs:1300
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    // On some platforms, the alignment of i32 is less than 4.
    #[repr(align(4))]
    struct AlignedI32(i32);

    let data = AlignedI32(42);
    let ptr = NonNull::<AlignedI32>::from(&data);

    assert!(ptr.is_aligned());
    assert!(!NonNull::new(ptr.as_ptr().wrapping_byte_add(1)).unwrap().is_aligned());
}
