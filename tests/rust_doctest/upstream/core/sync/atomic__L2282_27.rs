// Extracted from library/core/src/sync/atomic.rs:2282
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};
    
    let atom = AtomicPtr::<i64>::new(core::ptr::null_mut());
    assert_eq!(atom.fetch_byte_add(1, Ordering::Relaxed).addr(), 0);
    // Note: in units of bytes, not `size_of::<i64>()`.
    assert_eq!(atom.load(Ordering::Relaxed).addr(), 1);
}
