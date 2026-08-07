// Extracted from library/std/src/sync/mpmc/mod.rs:418
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc::channel;
    use std::time::Duration;

    let (tx, rx) = channel();

    tx.send_timeout(1, Duration::from_millis(400)).unwrap();
}
