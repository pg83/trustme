// Extracted from library/std/src/sync/mpsc.rs:823
#![allow(unused)]
fn main() {
    use std::sync::mpsc;
    use std::thread;
    use std::sync::mpsc::RecvError;

    let (send, recv) = mpsc::channel();
    let handle = thread::spawn(move || {
        send.send(1u8).unwrap();
        send.send(2).unwrap();
        send.send(3).unwrap();
        drop(send);
    });

    // wait for the thread to join so we ensure the sender is dropped
    handle.join().unwrap();

    assert_eq!(Ok(1), recv.recv());
    assert_eq!(Ok(2), recv.recv());
    assert_eq!(Ok(3), recv.recv());
    assert_eq!(Err(RecvError), recv.recv());
}
