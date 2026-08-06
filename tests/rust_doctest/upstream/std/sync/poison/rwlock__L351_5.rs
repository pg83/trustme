// Extracted from library/std/src/sync/poison/rwlock.rs:351
#![allow(unused)]
fn main() {
    use std::sync::{Arc, RwLock};
    use std::thread;
    
    let lock = Arc::new(RwLock::new(1));
    let c_lock = Arc::clone(&lock);
    
    let n = lock.read().unwrap();
    assert_eq!(*n, 1);
    
    thread::spawn(move || {
        let r = c_lock.read();
        assert!(r.is_ok());
    }).join().unwrap();
}
