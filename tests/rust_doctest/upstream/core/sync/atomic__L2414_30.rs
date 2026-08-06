// Extracted from library/core/src/sync/atomic.rs:2414
#![allow(unused)]
#![feature(strict_provenance_atomic_ptr)]
fn main() {
    use core::sync::atomic::{AtomicPtr, Ordering};
    
    let pointer = &mut 3i64 as *mut i64;
    // A tagged pointer
    let atom = AtomicPtr::<i64>::new(pointer.map_addr(|a| a | 1));
    assert_eq!(atom.fetch_or(1, Ordering::Relaxed).addr() & 1, 1);
    // Untag, and extract the previously tagged pointer.
    let untagged = atom.fetch_and(!1, Ordering::Relaxed)
        .map_addr(|a| a & !1);
    assert_eq!(untagged, pointer);
}
