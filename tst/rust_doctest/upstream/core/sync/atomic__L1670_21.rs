// Extracted from library/core/src/sync/atomic.rs:1670
#![allow(unused)]
fn main() {
    use std::sync::atomic::AtomicPtr;

    let mut data = 5;
    let atomic_ptr = AtomicPtr::new(&mut data);
    assert_eq!(unsafe { *atomic_ptr.into_inner() }, 5);
}
