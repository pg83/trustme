// Extracted from library/std/src/sync/mpmc/mod.rs:574
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc;
    use std::thread;

    let (send, _recv) = mpmc::sync_channel(3);
    let (tx1, tx2) = (send.clone(), send.clone());

    assert_eq!(tx1.capacity(), Some(3));

    let handle = thread::spawn(move || {
        tx2.send(1u8).unwrap();
    });

    handle.join().unwrap();

    assert_eq!(tx1.capacity(), Some(3));
}
