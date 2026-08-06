// Extracted from library/std/src/sync/poison/rwlock.rs:1087
#![allow(unused)]
#![feature(rwlock_downgrade)]
fn main() {
    use std::sync::{Arc, RwLock, RwLockWriteGuard};
    
    // The inner value starts as 0.
    let rw = Arc::new(RwLock::new(0));
    
    // Put the lock in write mode.
    let mut main_write_guard = rw.write().unwrap();
    
    let evil = rw.clone();
    let handle = std::thread::spawn(move || {
        // This will not return until the main thread drops the `main_read_guard`.
        let mut evil_guard = evil.write().unwrap();
    
        assert_eq!(*evil_guard, 1);
        *evil_guard = 2;
    });
    
    // After spawning the writer thread, set the inner value to 1.
    *main_write_guard = 1;
    
    // Atomically downgrade the write guard into a read guard.
    let main_read_guard = RwLockWriteGuard::downgrade(main_write_guard);
    
    // Since `downgrade` is atomic, the writer thread cannot have set the inner value to 2.
    assert_eq!(*main_read_guard, 1, "`downgrade` was not atomic");
    
    // Clean up everything now
    drop(main_read_guard);
    handle.join().unwrap();
    
    let final_check = rw.read().unwrap();
    assert_eq!(*final_check, 2);
}
