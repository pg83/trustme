// Extracted from library/std/src/sync/poison/condvar.rs:422
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex, Condvar};
    use std::thread;
    use std::time::Duration;

    let pair = Arc::new((Mutex::new(true), Condvar::new()));
    let pair2 = Arc::clone(&pair);

    thread::spawn(move || {
        let (lock, cvar) = &*pair2;
        let mut pending = lock.lock().unwrap();
        *pending = false;
        // We notify the condvar that the value has changed.
        cvar.notify_one();
    });

    // wait for the thread to start up
    let (lock, cvar) = &*pair;
    let result = cvar.wait_timeout_while(
        lock.lock().unwrap(),
        Duration::from_millis(100),
        |&mut pending| pending,
    ).unwrap();
    if result.1.timed_out() {
        // timed-out without the condition ever evaluating to false.
    }
    // access the locked mutex via result.0
}
