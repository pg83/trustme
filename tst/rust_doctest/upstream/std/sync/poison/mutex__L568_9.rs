// Extracted from library/std/src/sync/poison/mutex.rs:568
#![allow(unused)]
fn main() {
    use std::sync::Mutex;

    let mutex = Mutex::new(0);
    assert_eq!(mutex.into_inner().unwrap(), 0);
}
