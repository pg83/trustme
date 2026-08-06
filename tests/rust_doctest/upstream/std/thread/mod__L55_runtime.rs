// Extracted from library/std/src/thread/mod.rs:55
#![allow(unused)]
fn main() {
    use std::thread;
    
    let thread_join_handle = thread::spawn(move || {
        // some work here
    });
    // some work here
    let res = thread_join_handle.join();
}
