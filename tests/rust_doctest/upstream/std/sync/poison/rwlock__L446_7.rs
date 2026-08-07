// Extracted from library/std/src/sync/poison/rwlock.rs:446
#![allow(unused)]
fn main() {
    use std::sync::RwLock;

    let lock = RwLock::new(1);

    let mut n = lock.write().unwrap();
    *n = 2;

    assert!(lock.try_read().is_err());
}
