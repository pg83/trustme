// Extracted from library/core/src/sync/atomic.rs:2202
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};

    let atom = AtomicPtr::<i64>::new(core::ptr::null_mut());
    assert_eq!(atom.fetch_ptr_add(1, Ordering::Relaxed).addr(), 0);
    // Note: units of `size_of::<i64>()`.
    assert_eq!(atom.load(Ordering::Relaxed).addr(), 8);
}
