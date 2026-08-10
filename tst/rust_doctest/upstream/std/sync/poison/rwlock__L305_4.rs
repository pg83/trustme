// Extracted from library/std/src/sync/poison/rwlock.rs:305
#![allow(unused)]
#![feature(lock_value_accessors)]
fn main() {

    use std::sync::RwLock;

    let mut lock = RwLock::new(7);

    assert_eq!(lock.replace(11).unwrap(), 7);
    assert_eq!(lock.get_cloned().unwrap(), 11);
}
