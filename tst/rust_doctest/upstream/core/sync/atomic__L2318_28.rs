// Extracted from library/core/src/sync/atomic.rs:2318
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};

    let atom = AtomicPtr::<i64>::new(core::ptr::without_provenance_mut(1));
    assert_eq!(atom.fetch_byte_sub(1, Ordering::Relaxed).addr(), 1);
    assert_eq!(atom.load(Ordering::Relaxed).addr(), 0);
}
