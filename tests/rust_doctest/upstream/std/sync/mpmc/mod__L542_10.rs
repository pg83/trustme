// Extracted from library/std/src/sync/mpmc/mod.rs:542
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc;
    use std::thread;
    
    let (send, _recv) = mpmc::channel();
    let (tx1, tx2) = (send.clone(), send.clone());
    
    assert_eq!(tx1.len(), 0);
    
    let handle = thread::spawn(move || {
        tx2.send(1u8).unwrap();
    });
    
    handle.join().unwrap();
    
    assert_eq!(tx1.len(), 1);
}
