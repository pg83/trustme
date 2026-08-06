// Extracted from library/std/src/sync/mpsc.rs:807
#![allow(unused)]
fn main() {
    use std::sync::mpsc;
    use std::thread;
    
    let (send, recv) = mpsc::channel();
    let handle = thread::spawn(move || {
        send.send(1u8).unwrap();
    });
    
    handle.join().unwrap();
    
    assert_eq!(Ok(1), recv.recv());
}
