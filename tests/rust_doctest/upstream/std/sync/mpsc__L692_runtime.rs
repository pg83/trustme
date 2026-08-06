// Extracted from library/std/src/sync/mpsc.rs:692
#![allow(unused)]
fn main() {
    use std::sync::mpsc::sync_channel;
    use std::thread;
    
    // Create a sync_channel with buffer size 1
    let (sync_sender, receiver) = sync_channel(1);
    let sync_sender2 = sync_sender.clone();
    
    // First thread owns sync_sender
    thread::spawn(move || {
        sync_sender.send(1).unwrap();
        sync_sender.send(2).unwrap();
        // Thread blocked
    });
    
    // Second thread owns sync_sender2
    thread::spawn(move || {
        // This will return an error and send
        // no message if the buffer is full
        let _ = sync_sender2.try_send(3);
    });
    
    let mut msg;
    msg = receiver.recv().unwrap();
    println!("message {msg} received");
    
    msg = receiver.recv().unwrap();
    println!("message {msg} received");
    
    // Third message may have never been sent
    match receiver.try_recv() {
        Ok(msg) => println!("message {msg} received"),
        Err(_) => println!("the third message was never sent"),
    }
}
