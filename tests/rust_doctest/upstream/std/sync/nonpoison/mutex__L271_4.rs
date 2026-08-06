// Extracted from library/std/src/sync/nonpoison/mutex.rs:271
#![allow(unused)]
#![feature(nonpoison_mutex)]
fn main() {
    
    use std::sync::{Arc, nonpoison::Mutex};
    use std::thread;
    
    let mutex = Arc::new(Mutex::new(0));
    let c_mutex = Arc::clone(&mutex);
    
    thread::spawn(move || {
        *c_mutex.lock() = 10;
    }).join().expect("thread::spawn failed");
    assert_eq!(*mutex.lock(), 10);
}
