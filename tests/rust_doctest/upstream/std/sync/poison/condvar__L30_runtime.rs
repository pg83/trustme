// Extracted from library/std/src/sync/poison/condvar.rs:30
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Condvar, Mutex};
    use std::thread;
    use std::time::Duration;
    
    let pair = Arc::new((Mutex::new(false), Condvar::new()));
    let pair2 = Arc::clone(&pair);
    
    let handle =
    thread::spawn(move || {
        let (lock, cvar) = &*pair2;
    
        // Let's wait 20 milliseconds before notifying the condvar.
        thread::sleep(Duration::from_millis(20));
    
        let mut started = lock.lock().unwrap();
        // We update the boolean value.
        *started = true;
        cvar.notify_one();
    });
    
    // Wait for the thread to start up.
    let (lock, cvar) = &*pair;
    loop {
        // Let's put a timeout on the condvar's wait.
        let result = cvar.wait_timeout(lock.lock().unwrap(), Duration::from_millis(10)).unwrap();
        // 10 milliseconds have passed.
        if result.1.timed_out() {
            // timed out now and we can leave.
            break
        }
    }
    // Prevent leaks for Miri.
    let _ = handle.join();
}
