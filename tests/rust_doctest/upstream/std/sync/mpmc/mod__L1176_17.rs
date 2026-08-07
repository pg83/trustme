// Extracted from library/std/src/sync/mpmc/mod.rs:1176
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc;
    use std::thread;

    let (send, recv) = mpmc::channel();

    assert!(recv.is_empty());

    let handle = thread::spawn(move || {
        send.send(1u8).unwrap();
    });

    handle.join().unwrap();

    assert!(!recv.is_empty());
}
