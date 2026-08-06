// Extracted from library/std/src/sync/mpsc.rs:778
#![allow(unused)]
fn main() {
    use std::sync::mpsc::{Receiver, channel};
    
    let (_, receiver): (_, Receiver<i32>) = channel();
    
    assert!(receiver.try_recv().is_err());
}
