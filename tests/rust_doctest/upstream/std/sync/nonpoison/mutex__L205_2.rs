// Extracted from library/std/src/sync/nonpoison/mutex.rs:205
#![allow(unused)]
#![feature(nonpoison_mutex)]
#![feature(lock_value_accessors)]
fn main() {
    
    use std::sync::nonpoison::Mutex;
    
    let mut mutex = Mutex::new(7);
    
    assert_eq!(mutex.get_cloned(), 7);
    mutex.set(11);
    assert_eq!(mutex.get_cloned(), 11);
}
