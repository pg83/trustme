// Extracted from library/std/src/sync/mpmc/mod.rs:1302
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {

    use std::sync::mpmc;

    let (_, rx1) = mpmc::channel::<i32>();
    let (_, rx2) = mpmc::channel::<i32>();

    assert!(rx1.same_channel(&rx1));
    assert!(!rx1.same_channel(&rx2));
}
