// Extracted from library/std/src/sync/reentrant_lock.rs:247
#![allow(unused)]
#![feature(reentrant_lock)]
fn main() {

    use std::sync::ReentrantLock;

    let lock = ReentrantLock::new(0);
    assert_eq!(lock.into_inner(), 0);
}
