#![feature(strict_provenance_atomic_ptr)]

use std::sync::atomic::{AtomicPtr, Ordering};

fn main() {
    let atom = AtomicPtr::<i32>::new(std::ptr::null_mut());

    assert_eq!(atom.fetch_ptr_add(2, Ordering::Relaxed).addr(), 0);
    assert_eq!(atom.fetch_ptr_sub(1, Ordering::Relaxed).addr(), 8);
    assert_eq!(atom.load(Ordering::Relaxed).addr(), 4);
}
