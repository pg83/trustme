// Extracted from library/std/src/sync/mpmc/mod.rs:906
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc::{Receiver, channel};

    let (_, receiver): (_, Receiver<i32>) = channel();

    assert!(receiver.try_recv().is_err());
}
