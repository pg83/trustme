// Extracted from library/core/src/mem/maybe_uninit.rs:1038
#![allow(unused)]
#![feature(maybe_uninit_write_slice)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut dst = [MaybeUninit::uninit(); 32];
    let src = [0; 32];
    
    let init = dst.write_copy_of_slice(&src);
    
    assert_eq!(init, src);
}
