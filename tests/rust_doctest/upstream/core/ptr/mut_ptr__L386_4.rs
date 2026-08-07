// Extracted from library/core/src/ptr/mut_ptr.rs:386
#![allow(unused)]
fn main() {
    let mut s = [1, 2, 3];
    let ptr: *mut u32 = s.as_mut_ptr();

    unsafe {
        assert_eq!(2, *ptr.offset(1));
        assert_eq!(3, *ptr.offset(2));
    }
}
