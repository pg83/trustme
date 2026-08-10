// Extracted from library/std/src/sync/mpsc.rs:357
#![allow(unused)]
fn main() {
    use std::sync::mpsc::sync_channel;
    use std::thread;

    // Create a sync_channel with buffer size 2
    let (sync_sender, receiver) = sync_channel(2);
    let sync_sender2 = sync_sender.clone();

    // First thread owns sync_sender
    thread::spawn(move || {
        sync_sender.send(1).unwrap();
        sync_sender.send(2).unwrap();
    });

    // Second thread owns sync_sender2
    thread::spawn(move || {
        sync_sender2.send(3).unwrap();
        // thread will now block since the buffer is full
        println!("Thread unblocked!");
    });

    let mut msg;

    msg = receiver.recv().unwrap();
    println!("message {msg} received");

    // "Thread unblocked!" will be printed now

    msg = receiver.recv().unwrap();
    println!("message {msg} received");

    msg = receiver.recv().unwrap();

    println!("message {msg} received");
}
