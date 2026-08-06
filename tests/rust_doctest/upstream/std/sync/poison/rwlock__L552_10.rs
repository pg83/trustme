// Extracted from library/std/src/sync/poison/rwlock.rs:552
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
    let guard = lock.write().unwrap_or_else(|mut e| {
        **e.get_mut() = 1;
        lock.clear_poison();
        e.into_inner()
    });
    assert_eq!(lock.is_poisoned(), false);
    assert_eq!(*guard, 1);
}
