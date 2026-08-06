// Extracted from library/std/src/sync/mpsc.rs:49
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::mpsc::channel;
    
    // Create a simple streaming channel
    let (tx, rx) = channel();
    thread::spawn(move || {
        tx.send(10).unwrap();
    });
    assert_eq!(rx.recv().unwrap(), 10);
}
