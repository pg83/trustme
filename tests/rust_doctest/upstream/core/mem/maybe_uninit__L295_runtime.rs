// Extracted from library/core/src/mem/maybe_uninit.rs:295
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    let v: MaybeUninit<Vec<u8>> = MaybeUninit::new(vec![42]);
    // Prevent leaks for Miri
    unsafe { let _ = MaybeUninit::assume_init(v); }
}
