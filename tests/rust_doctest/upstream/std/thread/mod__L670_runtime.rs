// Extracted from library/std/src/thread/mod.rs:670
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::mpsc::channel;
    
    let (tx, rx) = channel();
    
    let sender = thread::spawn(move || {
        tx.send("Hello, thread".to_owned())
            .expect("Unable to send on channel");
    });
    
    let receiver = thread::spawn(move || {
        let value = rx.recv().expect("Unable to receive from channel");
        println!("{value}");
    });
    
    sender.join().expect("The sender thread has panicked");
    receiver.join().expect("The receiver thread has panicked");
}
