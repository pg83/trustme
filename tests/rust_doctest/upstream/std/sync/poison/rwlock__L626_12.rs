// Extracted from library/std/src/sync/poison/rwlock.rs:626
#![allow(unused)]
fn main() {
    use std::sync::RwLock;

    let mut lock = RwLock::new(0);
    *lock.get_mut().unwrap() = 10;
    assert_eq!(*lock.read().unwrap(), 10);
}
