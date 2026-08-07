// Extracted from library/std/src/thread/mod.rs:1060
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::{Arc, atomic::{Ordering, AtomicBool}};
    use std::time::Duration;

    let flag = Arc::new(AtomicBool::new(false));
    let flag2 = Arc::clone(&flag);

    let parked_thread = thread::spawn(move || {
        // We want to wait until the flag is set. We *could* just spin, but using
        // park/unpark is more efficient.
        while !flag2.load(Ordering::Relaxed) {
            println!("Parking thread");
            thread::park();
            // We *could* get here spuriously, i.e., way before the 10ms below are over!
            // But that is no problem, we are in a loop until the flag is set anyway.
            println!("Thread unparked");
        }
        println!("Flag received");
    });

    // Let some time pass for the thread to be spawned.
    thread::sleep(Duration::from_millis(10));

    // Set the flag, and let the thread wake up.
    // There is no race condition here, if `unpark`
    // happens first, `park` will return immediately.
    // Hence there is no risk of a deadlock.
    flag.store(true, Ordering::Relaxed);
    println!("Unpark the thread");
    parked_thread.thread().unpark();

    parked_thread.join().unwrap();
}
