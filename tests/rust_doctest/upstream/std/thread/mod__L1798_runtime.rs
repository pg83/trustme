// Extracted from library/std/src/thread/mod.rs:1798
#![allow(unused)]
fn main() {
    use std::thread;
    
    let join_handle: thread::JoinHandle<_> = thread::spawn(|| {
        // some work here
    });
}
