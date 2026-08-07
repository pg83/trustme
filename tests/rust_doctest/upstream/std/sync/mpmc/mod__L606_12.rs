// Extracted from library/std/src/sync/mpmc/mod.rs:606
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc;

    let (tx1, _) = mpmc::channel::<i32>();
    let (tx2, _) = mpmc::channel::<i32>();

    assert!(tx1.same_channel(&tx1));
    assert!(!tx1.same_channel(&tx2));
}
