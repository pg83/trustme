// Extracted from library/std/src/thread/mod.rs:1493
#![allow(unused)]
fn main() {
    use std::thread;
    use std::time::Duration;
    
    let parked_thread = thread::Builder::new()
        .spawn(|| {
            println!("Parking thread");
            thread::park();
            println!("Thread unparked");
        })
        .unwrap();
    
    // Let some time pass for the thread to be spawned.
    thread::sleep(Duration::from_millis(10));
    
    println!("Unpark the thread");
    parked_thread.thread().unpark();
    
    parked_thread.join().unwrap();
}
