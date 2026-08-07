// Extracted from library/std/src/sync/poison/condvar.rs:354
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex, Condvar};
    use std::thread;
    use std::time::Duration;

    let pair = Arc::new((Mutex::new(false), Condvar::new()));
    let pair2 = Arc::clone(&pair);

    thread::spawn(move || {
        let (lock, cvar) = &*pair2;
        let mut started = lock.lock().unwrap();
        *started = true;
        // We notify the condvar that the value has changed.
        cvar.notify_one();
    });

    // wait for the thread to start up
    let (lock, cvar) = &*pair;
    let mut started = lock.lock().unwrap();
    // as long as the value inside the `Mutex<bool>` is `false`, we wait
    loop {
        let result = cvar.wait_timeout(started, Duration::from_millis(10)).unwrap();
        // 10 milliseconds have passed, or maybe the value changed!
        started = result.0;
        if *started == true {
            // We received the notification and the value has been updated, we can leave.
            break
        }
    }
}
