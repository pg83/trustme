// Extracted from library/core/src/ptr/mut_ptr.rs:1875
#![allow(unused)]
#![feature(slice_ptr_get)]
fn main() {

    let x = &mut [1, 2, 4] as *mut [i32];

    unsafe {
        assert_eq!(x.get_unchecked_mut(1), x.as_mut_ptr().add(1));
    }
}
