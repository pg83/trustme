// Extracted from library/core/src/mem/maybe_uninit.rs:957
#![allow(unused)]
#![feature(maybe_uninit_as_bytes, maybe_uninit_slice)]
fn main() {
    use std::mem::MaybeUninit;
    
    let val = 0x12345678_i32;
    let uninit = MaybeUninit::new(val);
    let uninit_bytes = uninit.as_bytes();
    let bytes = unsafe { uninit_bytes.assume_init_ref() };
    assert_eq!(bytes, val.to_ne_bytes());
}
