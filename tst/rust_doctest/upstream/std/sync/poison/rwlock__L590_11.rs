// Extracted from library/std/src/sync/poison/rwlock.rs:590
#![allow(unused)]
fn main() {
    use std::sync::RwLock;

    let lock = RwLock::new(String::new());
    {
        let mut s = lock.write().unwrap();
        *s = "modified".to_owned();
    }
    assert_eq!(lock.into_inner().unwrap(), "modified");
}
