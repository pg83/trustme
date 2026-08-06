// Extracted from library/core/src/mem/maybe_uninit.rs:741
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut x = MaybeUninit::<Vec<u32>>::uninit();
    let mut x_mu = x;
    let mut x = &mut x_mu;
    // Initialize `x`:
    x.write(vec![1, 2, 3]);
    // Now that our `MaybeUninit<_>` is known to be initialized, it is okay to
    // create a shared reference to it:
    let x: &Vec<u32> = unsafe {
        // SAFETY: `x` has been initialized.
        x.assume_init_ref()
    };
    assert_eq!(x, &vec![1, 2, 3]);
    // Prevent leaks for Miri
    unsafe { MaybeUninit::assume_init_drop(&mut x_mu); }
}
