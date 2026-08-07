// Extracted from library/std/src/sync/mpsc.rs:655
#![allow(unused)]
fn main() {
    use std::sync::mpsc::sync_channel;
    use std::thread;

    // Create a rendezvous sync_channel with buffer size 0
    let (sync_sender, receiver) = sync_channel(0);

    thread::spawn(move || {
       println!("sending message...");
       sync_sender.send(1).unwrap();
       // Thread is now blocked until the message is received

       println!("...message received!");
    });

    let msg = receiver.recv().unwrap();
    assert_eq!(1, msg);
}
