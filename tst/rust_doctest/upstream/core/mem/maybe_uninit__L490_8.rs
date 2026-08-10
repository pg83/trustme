// Extracted from library/core/src/mem/maybe_uninit.rs:490
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let mut x = MaybeUninit::<Vec<u32>>::uninit();
    x.write(vec![0, 1, 2]);
    // Create a reference into the `MaybeUninit<T>`. This is okay because we initialized it.
    let x_vec = unsafe { &*x.as_ptr() };
    assert_eq!(x_vec.len(), 3);
    // Prevent leaks for Miri
    unsafe { MaybeUninit::assume_init_drop(&mut x); }
}
