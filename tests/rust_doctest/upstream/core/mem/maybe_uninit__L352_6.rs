// Extracted from library/core/src/mem/maybe_uninit.rs:352
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let x = MaybeUninit::<(u8, bool)>::zeroed();
    let x = unsafe { x.assume_init() };
    assert_eq!(x, (0, false));
}
