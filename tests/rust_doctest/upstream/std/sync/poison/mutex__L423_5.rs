// Extracted from library/std/src/sync/poison/mutex.rs:423
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    use std::thread;
    
    let mutex = Arc::new(Mutex::new(0));
    let c_mutex = Arc::clone(&mutex);
    
    thread::spawn(move || {
        *c_mutex.lock().unwrap() = 10;
    }).join().expect("thread::spawn failed");
    assert_eq!(*mutex.lock().unwrap(), 10);
}
