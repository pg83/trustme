// Extracted from library/core/src/sync/atomic.rs:2243
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};
    
    let array = [1i32, 2i32];
    let atom = AtomicPtr::new(array.as_ptr().wrapping_add(1) as *mut _);
    
    assert!(core::ptr::eq(
        atom.fetch_ptr_sub(1, Ordering::Relaxed),
        &array[1],
    ));
    assert!(core::ptr::eq(atom.load(Ordering::Relaxed), &array[0]));
}
