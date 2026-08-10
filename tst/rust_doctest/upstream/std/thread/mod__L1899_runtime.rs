// Extracted from library/std/src/thread/mod.rs:1899
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new();

    let join_handle: thread::JoinHandle<_> = builder.spawn(|| {
        // some work here
    }).unwrap();
    join_handle.join().expect("Couldn't join on the associated thread");
}
