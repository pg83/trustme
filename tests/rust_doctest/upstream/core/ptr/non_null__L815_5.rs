// Extracted from library/core/src/ptr/non_null.rs:815
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let a = [0; 5];
    let ptr1: NonNull<u32> = NonNull::from(&a[1]);
    let ptr2: NonNull<u32> = NonNull::from(&a[3]);
    unsafe {
        assert_eq!(ptr2.offset_from(ptr1), 2);
        assert_eq!(ptr1.offset_from(ptr2), -2);
        assert_eq!(ptr1.offset(2), ptr2);
        assert_eq!(ptr2.offset(-2), ptr1);
    }
}
