// Extracted from library/std/src/sync/mpsc.rs:98
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::mpsc::sync_channel;

    let (tx, rx) = sync_channel::<i32>(0);
    thread::spawn(move || {
        // This will wait for the parent thread to start receiving
        tx.send(53).unwrap();
    });
    rx.recv().unwrap();
}
