// Extracted from library/core/src/mem/maybe_uninit.rs:71
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    // Create an explicitly uninitialized reference. The compiler knows that data inside
    // a `MaybeUninit<T>` may be invalid, and hence this is not UB:
    let mut x = MaybeUninit::<&i32>::uninit();
    // Set it to a valid value.
    x.write(&0);
    // Extract the initialized data -- this is only allowed *after* properly
    // initializing `x`!
    let x = unsafe { x.assume_init() };
}
