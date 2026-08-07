// Extracted from library/std/src/sync/poison.rs:303
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    use std::sync::{Arc, Mutex};
    use std::thread;

    let mutex = Arc::new(Mutex::new(HashSet::new()));

    // poison the mutex
    let c_mutex = Arc::clone(&mutex);
    let _ = thread::spawn(move || {
        let mut data = c_mutex.lock().unwrap();
        data.insert(10);
        panic!();
    }).join();

    let p_err = mutex.lock().unwrap_err();
    let data = p_err.into_inner();
    println!("recovered {} items", data.len());
}
