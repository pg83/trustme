// Extracted from library/core/src/ptr/non_null.rs:208
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    let mut x = 0u32;
    let ptr = unsafe { NonNull::new_unchecked(&mut x as *mut _) };
}
