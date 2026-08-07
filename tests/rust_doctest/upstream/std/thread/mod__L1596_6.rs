// Extracted from library/std/src/thread/mod.rs:1596
#![allow(unused)]
#![feature(thread_raw)]
fn main() {

    use std::thread::{self, Thread};

    let thread = thread::current();
    let id = thread.id();
    let ptr = Thread::into_raw(thread);
    unsafe {
        assert_eq!(Thread::from_raw(ptr).id(), id);
    }
}
