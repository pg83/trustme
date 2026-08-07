// Extracted from library/std/src/sync/poison/mutex.rs:379
#![allow(unused)]
#![feature(lock_value_accessors)]
fn main() {

    use std::sync::Mutex;

    let mut mutex = Mutex::new(7);

    assert_eq!(mutex.replace(11).unwrap(), 7);
    assert_eq!(mutex.get_cloned().unwrap(), 11);
}
