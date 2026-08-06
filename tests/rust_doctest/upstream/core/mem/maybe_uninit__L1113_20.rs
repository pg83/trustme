// Extracted from library/core/src/mem/maybe_uninit.rs:1113
#![allow(unused)]
#![feature(maybe_uninit_write_slice)]
fn main() {
    
    let mut vec = Vec::with_capacity(32);
    let src = ["rust", "is", "a", "pretty", "cool", "language"].map(|s| s.to_string());
    
    vec.spare_capacity_mut()[..src.len()].write_clone_of_slice(&src);
    
    // SAFETY: we have just cloned all the elements of len into the spare capacity
    // the first src.len() elements of the vec are valid now.
    unsafe {
        vec.set_len(src.len());
    }
    
    assert_eq!(vec, src);
}
