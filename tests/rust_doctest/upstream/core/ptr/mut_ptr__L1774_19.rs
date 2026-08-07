// Extracted from library/core/src/ptr/mut_ptr.rs:1774
#![allow(unused)]
#![feature(raw_slice_split)]
#![feature(slice_ptr_get)]
fn main() {

    let mut v = [1, 0, 3, 0, 5, 6];
    let ptr = &mut v as *mut [_];
    unsafe {
        let (left, right) = ptr.split_at_mut(2);
        assert_eq!(&*left, [1, 0]);
        assert_eq!(&*right, [3, 0, 5, 6]);
    }
}
