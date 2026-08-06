// Extracted from library/std/src/sync/mpmc/mod.rs:474
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc;
    use std::thread;
    
    let (send, _recv) = mpmc::channel();
    
    let tx1 = send.clone();
    let tx2 = send.clone();
    
    assert!(tx1.is_empty());
    
    let handle = thread::spawn(move || {
        tx2.send(1u8).unwrap();
    });
    
    handle.join().unwrap();
    
    assert!(!tx1.is_empty());
}
