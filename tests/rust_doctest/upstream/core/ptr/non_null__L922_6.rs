// Extracted from library/core/src/ptr/non_null.rs:922
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let a = [0; 5];
    let ptr1: NonNull<u32> = NonNull::from(&a[1]);
    let ptr2: NonNull<u32> = NonNull::from(&a[3]);
    unsafe {
        assert_eq!(ptr2.offset_from_unsigned(ptr1), 2);
        assert_eq!(ptr1.add(2), ptr2);
        assert_eq!(ptr2.sub(2), ptr1);
        assert_eq!(ptr2.offset_from_unsigned(ptr2), 0);
    }
    
    // This would be incorrect, as the pointers are not correctly ordered:
    // ptr1.offset_from_unsigned(ptr2)
}
