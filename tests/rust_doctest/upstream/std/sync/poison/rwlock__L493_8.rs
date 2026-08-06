// Extracted from library/std/src/sync/poison/rwlock.rs:493
#![allow(unused)]
fn main() {
    use std::sync::RwLock;
    
    let lock = RwLock::new(1);
    
    let n = lock.read().unwrap();
    assert_eq!(*n, 1);
    
    assert!(lock.try_write().is_err());
}
