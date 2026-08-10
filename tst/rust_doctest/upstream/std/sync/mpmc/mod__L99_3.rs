// Extracted from library/std/src/sync/mpmc/mod.rs:99
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc::channel;

    // The call to recv() will return an error because the channel has already
    // hung up (or been deallocated)
    let (tx, rx) = channel::<i32>();
    drop(tx);
    assert!(rx.recv().is_err());
}
