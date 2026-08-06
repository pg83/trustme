// Extracted from library/std/src/sync/mpmc/mod.rs:378
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc::channel;
    
    let (tx, rx) = channel();
    
    // This send is always successful
    tx.send(1).unwrap();
    
    // This send will fail because the receiver is gone
    drop(rx);
    assert!(tx.send(1).is_err());
}
