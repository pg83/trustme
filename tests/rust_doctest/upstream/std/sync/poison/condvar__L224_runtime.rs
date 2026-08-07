// Extracted from library/std/src/sync/poison/condvar.rs:224
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex, Condvar};
    use std::thread;

    let pair = Arc::new((Mutex::new(true), Condvar::new()));
    let pair2 = Arc::clone(&pair);

    thread::spawn(move || {
        let (lock, cvar) = &*pair2;
        let mut pending = lock.lock().unwrap();
        *pending = false;
        // We notify the condvar that the value has changed.
        cvar.notify_one();
    });

    // Wait for the thread to start up.
    let (lock, cvar) = &*pair;
    // As long as the value inside the `Mutex<bool>` is `true`, we wait.
    let _guard = cvar.wait_while(lock.lock().unwrap(), |pending| { *pending }).unwrap();
}
