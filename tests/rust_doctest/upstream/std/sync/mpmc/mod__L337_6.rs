// Extracted from library/std/src/sync/mpmc/mod.rs:337
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc::{channel, Receiver, Sender};
    
    let (sender, _receiver): (Sender<i32>, Receiver<i32>) = channel();
    
    assert!(sender.try_send(1).is_ok());
}
