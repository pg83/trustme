// Extracted from library/std/src/sync/mpmc/mod.rs:940
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc;
    use std::thread;
    
    let (send, recv) = mpmc::channel();
    let handle = thread::spawn(move || {
        send.send(1u8).unwrap();
    });
    
    handle.join().unwrap();
    
    assert_eq!(Ok(1), recv.recv());
}
