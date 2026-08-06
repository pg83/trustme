// Extracted from library/std/src/sync/nonpoison/mutex.rs:333
#![allow(unused)]
#![feature(nonpoison_mutex)]
fn main() {
    
    use std::sync::nonpoison::Mutex;
    
    let mutex = Mutex::new(0);
    assert_eq!(mutex.into_inner(), 0);
}
