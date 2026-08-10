// Extracted from library/std/src/sync/poison/rwlock.rs:402
#![allow(unused)]
fn main() {
    use std::sync::RwLock;

    let lock = RwLock::new(1);

    match lock.try_read() {
        Ok(n) => assert_eq!(*n, 1),
        Err(_) => unreachable!(),
    };
}
