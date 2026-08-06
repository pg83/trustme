// Extracted from library/std/src/sync/mpsc.rs:551
#![allow(unused)]
fn main() {
    use std::sync::mpsc::sync_channel;
    use std::thread;
    
    let (sender, receiver) = sync_channel(1);
    
    // this returns immediately
    sender.send(1).unwrap();
    
    thread::spawn(move || {
        // this will block until the previous message has been received
        sender.send(2).unwrap();
    });
    
    assert_eq!(receiver.recv().unwrap(), 1);
    assert_eq!(receiver.recv().unwrap(), 2);
}
