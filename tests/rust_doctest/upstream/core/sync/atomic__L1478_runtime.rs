// Extracted from library/core/src/sync/atomic.rs:1478
#![allow(unused)]
fn main() {
    use std::sync::atomic::AtomicPtr;

    let ptr = &mut 5;
    let atomic_ptr = AtomicPtr::new(ptr);
}
