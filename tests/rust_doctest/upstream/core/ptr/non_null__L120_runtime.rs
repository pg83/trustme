// Extracted from library/core/src/ptr/non_null.rs:120
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let ptr = NonNull::<u32>::dangling();
    // Important: don't try to access the value of `ptr` without
    // initializing it first! The pointer is not null but isn't valid either!
}
