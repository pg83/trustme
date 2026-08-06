// Extracted from library/std/src/sync/poison/rwlock.rs:523
#![allow(unused)]
fn main() {
    use std::sync::{Arc, RwLock};
    use std::thread;
    
    let lock = Arc::new(RwLock::new(0));
    let c_lock = Arc::clone(&lock);
    
    let _ = thread::spawn(move || {
        let _lock = c_lock.write().unwrap();
        panic!(); // the lock gets poisoned
    }).join();
    assert_eq!(lock.is_poisoned(), true);
}
