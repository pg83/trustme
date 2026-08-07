// Extracted from library/core/src/sync/atomic.rs:2363
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};

    let pointer = &mut 3i64 as *mut i64;

    let atom = AtomicPtr::<i64>::new(pointer);
    // Tag the bottom bit of the pointer.
    assert_eq!(atom.fetch_or(1, Ordering::Relaxed).addr() & 1, 0);
    // Extract and untag.
    let tagged = atom.load(Ordering::Relaxed);
    assert_eq!(tagged.addr() & 1, 1);
    assert_eq!(tagged.map_addr(|p| p & !1), pointer);
}
