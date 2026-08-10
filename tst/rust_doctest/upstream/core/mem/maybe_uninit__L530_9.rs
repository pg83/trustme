// Extracted from library/core/src/mem/maybe_uninit.rs:530
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;

    let mut x = MaybeUninit::<Vec<u32>>::uninit();
    x.write(vec![0, 1, 2]);
    // Create a reference into the `MaybeUninit<Vec<u32>>`.
    // This is okay because we initialized it.
    let x_vec = unsafe { &mut *x.as_mut_ptr() };
    x_vec.push(3);
    assert_eq!(x_vec.len(), 4);
    // Prevent leaks for Miri
    unsafe { MaybeUninit::assume_init_drop(&mut x); }
}
