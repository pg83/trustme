// Extracted from library/core/src/ptr/const_ptr.rs:736
#![allow(unused)]
fn main() {
    let a = [0; 5];
    let ptr1: *const i32 = &a[1];
    let ptr2: *const i32 = &a[3];
    unsafe {
        assert_eq!(ptr2.offset_from_unsigned(ptr1), 2);
        assert_eq!(ptr1.add(2), ptr2);
        assert_eq!(ptr2.sub(2), ptr1);
        assert_eq!(ptr2.offset_from_unsigned(ptr2), 0);
    }

    // This would be incorrect, as the pointers are not correctly ordered:
    // ptr1.offset_from_unsigned(ptr2)
}
