// Extracted from library/std/src/thread/mod.rs:1808
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new();

    let join_handle: thread::JoinHandle<_> = builder.spawn(|| {
        // some work here
    }).unwrap();
}
