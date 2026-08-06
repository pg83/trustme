// Extracted from library/std/src/sync/reentrant_lock.rs:228
#![allow(unused)]
#![feature(reentrant_lock)]
fn main() {
    use std::sync::ReentrantLock;
    
    let lock = ReentrantLock::new(0);
}
