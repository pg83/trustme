// Extracted from library/std/src/sync/reentrant_lock.rs:272
#![allow(unused)]
#![feature(reentrant_lock)]
fn main() {
    use std::cell::Cell;
    use std::sync::{Arc, ReentrantLock};
    use std::thread;
    
    let lock = Arc::new(ReentrantLock::new(Cell::new(0)));
    let c_lock = Arc::clone(&lock);
    
    thread::spawn(move || {
        c_lock.lock().set(10);
    }).join().expect("thread::spawn failed");
    assert_eq!(lock.lock().get(), 10);
}
