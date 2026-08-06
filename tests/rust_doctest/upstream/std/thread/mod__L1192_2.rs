// Extracted from library/std/src/thread/mod.rs:1192
#![allow(unused)]
fn main() {
    use std::thread;
    
    let other_thread = thread::spawn(|| {
        thread::current().id()
    });
    
    let other_thread_id = other_thread.join().unwrap();
    assert!(thread::current().id() != other_thread_id);
}
