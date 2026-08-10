// Extracted from library/core/src/sync/atomic.rs:1564
#![allow(unused)]
#![feature(atomic_from_mut)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};

    let mut data = 123;
    let mut some_ptr = &mut data as *mut i32;
    let a = AtomicPtr::from_mut(&mut some_ptr);
    let mut other_data = 456;
    a.store(&mut other_data, Ordering::Relaxed);
    assert_eq!(unsafe { *some_ptr }, 456);
}
