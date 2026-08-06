// Extracted from library/std/src/sync/mpmc/mod.rs:448
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc::channel;
    use std::time::{Duration, Instant};
    
    let (tx, rx) = channel();
    
    let t = Instant::now() + Duration::from_millis(400);
    tx.send_deadline(1, t).unwrap();
}
