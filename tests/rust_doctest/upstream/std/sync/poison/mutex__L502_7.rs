// Extracted from library/std/src/sync/poison/mutex.rs:502
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    use std::thread;
    
    let mutex = Arc::new(Mutex::new(0));
    let c_mutex = Arc::clone(&mutex);
    
    let _ = thread::spawn(move || {
        let _lock = c_mutex.lock().unwrap();
        panic!(); // the mutex gets poisoned
    }).join();
    assert_eq!(mutex.is_poisoned(), true);
}
