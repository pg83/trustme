// Extracted from library/core/src/mem/maybe_uninit.rs:653
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let mut x = MaybeUninit::<u32>::uninit();
    x.write(13);
    let x1 = unsafe { x.assume_init_read() };
    // `u32` is `Copy`, so we may read multiple times.
    let x2 = unsafe { x.assume_init_read() };
    assert_eq!(x1, x2);

    let mut x = MaybeUninit::<Option<Vec<u32>>>::uninit();
    x.write(None);
    let x1 = unsafe { x.assume_init_read() };
    // Duplicating a `None` value is okay, so we may read multiple times.
    let x2 = unsafe { x.assume_init_read() };
    assert_eq!(x1, x2);
}
