// Extracted from library/std/src/thread/mod.rs:1548
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new();

    let handler = builder.spawn(|| {
        assert!(thread::current().name().is_none());
    }).unwrap();

    handler.join().unwrap();
}
