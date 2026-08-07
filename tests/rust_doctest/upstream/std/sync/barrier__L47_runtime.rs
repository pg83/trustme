// Extracted from library/std/src/sync/barrier.rs:47
#![allow(unused)]
fn main() {
    use std::sync::Barrier;

    let barrier = Barrier::new(1);
    let barrier_wait_result = barrier.wait();
}
