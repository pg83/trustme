// Extracted from library/core/src/ptr/mut_ptr.rs:910
#![allow(unused)]
fn main() {
    let mut a = [0; 5];
    let p: *mut i32 = a.as_mut_ptr();
    unsafe {
        let ptr1: *mut i32 = p.add(1);
        let ptr2: *mut i32 = p.add(3);

        assert_eq!(ptr2.offset_from_unsigned(ptr1), 2);
        assert_eq!(ptr1.add(2), ptr2);
        assert_eq!(ptr2.sub(2), ptr1);
        assert_eq!(ptr2.offset_from_unsigned(ptr2), 0);
    }

    // This would be incorrect, as the pointers are not correctly ordered:
    // ptr1.offset_from(ptr2)
}
