// Extracted from library/core/src/mem/maybe_uninit.rs:1050
#![allow(unused)]
#![feature(maybe_uninit_write_slice)]
fn main() {

    let mut vec = Vec::with_capacity(32);
    let src = [0; 16];

    vec.spare_capacity_mut()[..src.len()].write_copy_of_slice(&src);

    // SAFETY: we have just copied all the elements of len into the spare capacity
    // the first src.len() elements of the vec are valid now.
    unsafe {
        vec.set_len(src.len());
    }

    assert_eq!(vec, src);
}
