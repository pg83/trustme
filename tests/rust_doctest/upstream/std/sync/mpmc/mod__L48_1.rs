// Extracted from library/std/src/sync/mpmc/mod.rs:48
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::thread;
    use std::sync::mpmc::channel;

    // Create a simple streaming channel
    let (tx, rx) = channel();
    thread::spawn(move || {
        tx.send(10).unwrap();
    });
    assert_eq!(rx.recv().unwrap(), 10);
}
