// Extracted from library/core/src/ptr/non_null.rs:1262
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    unsafe {
    let x = [5_u8, 6, 7, 8, 9];
    let ptr = NonNull::new(x.as_ptr() as *mut u8).unwrap();
    let offset = ptr.align_offset(align_of::<u16>());

    if offset < x.len() - 1 {
        let u16_ptr = ptr.add(offset).cast::<u16>();
        assert!(u16_ptr.read() == u16::from_ne_bytes([5, 6]) || u16_ptr.read() == u16::from_ne_bytes([6, 7]));
    } else {
        // while the pointer can be aligned via `offset`, it would point
        // outside the allocation
    }
    }
}
