// Extracted from library/std/src/sync/nonpoison/mutex.rs:24
#![allow(unused)]
#![feature(nonpoison_mutex)]
fn main() {
    
    use std::thread;
    use std::sync::{Arc, nonpoison::Mutex};
    
    let mutex = Arc::new(Mutex::new(0u32));
    let mut handles = Vec::new();
    
    for n in 0..10 {
        let m = Arc::clone(&mutex);
        let handle = thread::spawn(move || {
            let mut guard = m.lock();
            *guard += 1;
            panic!("panic from thread {n} {guard}")
        });
        handles.push(handle);
    }
    
    for h in handles {
        let _ = h.join();
    }
    
    println!("Finished, locked {} times", mutex.lock());
}
