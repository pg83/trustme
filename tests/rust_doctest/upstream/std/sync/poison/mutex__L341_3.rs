// Extracted from library/std/src/sync/poison/mutex.rs:341
#![allow(unused)]
#![feature(lock_value_accessors)]
fn main() {
    
    use std::sync::Mutex;
    
    let mut mutex = Mutex::new(7);
    
    assert_eq!(mutex.get_cloned().unwrap(), 7);
    mutex.set(11).unwrap();
    assert_eq!(mutex.get_cloned().unwrap(), 11);
}
