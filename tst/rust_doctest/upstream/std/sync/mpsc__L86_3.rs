// Extracted from library/std/src/sync/mpsc.rs:86
#![allow(unused)]
fn main() {
    use std::sync::mpsc::channel;

    // The call to recv() will return an error because the channel has already
    // hung up (or been deallocated)
    let (tx, rx) = channel::<i32>();
    drop(tx);
    assert!(rx.recv().is_err());
}
