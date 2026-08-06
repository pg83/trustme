// Extracted from library/core/src/sync/atomic.rs:2464
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};
    
    let pointer = &mut 3i64 as *mut i64;
    let atom = AtomicPtr::<i64>::new(pointer);
    
    // Toggle a tag bit on the pointer.
    atom.fetch_xor(1, Ordering::Relaxed);
    assert_eq!(atom.load(Ordering::Relaxed).addr() & 1, 1);
}
