// Extracted from library/core/src/ptr/mut_ptr.rs:1816
#![allow(unused)]
#![feature(raw_slice_split)]
fn main() {
    
    let mut v = [1, 0, 3, 0, 5, 6];
    // scoped to restrict the lifetime of the borrows
    unsafe {
        let ptr = &mut v as *mut [_];
        let (left, right) = ptr.split_at_mut_unchecked(2);
        assert_eq!(&*left, [1, 0]);
        assert_eq!(&*right, [3, 0, 5, 6]);
        (&mut *left)[1] = 2;
        (&mut *right)[1] = 4;
    }
    assert_eq!(v, [1, 2, 3, 4, 5, 6]);
}
