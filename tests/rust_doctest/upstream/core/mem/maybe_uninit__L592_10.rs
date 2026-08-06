// Extracted from library/core/src/mem/maybe_uninit.rs:592
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut x = MaybeUninit::<bool>::uninit();
    x.write(true);
    let x_init = unsafe { x.assume_init() };
    assert_eq!(x_init, true);
}
