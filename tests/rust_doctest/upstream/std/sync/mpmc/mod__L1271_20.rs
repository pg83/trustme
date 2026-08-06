// Extracted from library/std/src/sync/mpmc/mod.rs:1271
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc;
    use std::thread;
    
    let (send, recv) = mpmc::sync_channel(3);
    
    assert_eq!(recv.capacity(), Some(3));
    
    let handle = thread::spawn(move || {
        send.send(1u8).unwrap();
    });
    
    handle.join().unwrap();
    
    assert_eq!(recv.capacity(), Some(3));
}
