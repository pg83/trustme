// Extracted from library/std/src/sync/reentrant_lock.rs:312
#![allow(unused)]
#![feature(reentrant_lock)]
fn main() {
    use std::sync::ReentrantLock;

    let mut lock = ReentrantLock::new(0);
    *lock.get_mut() = 10;
    assert_eq!(*lock.lock(), 10);
}
