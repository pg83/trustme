// Extracted from library/std/src/thread/mod.rs:654
#![allow(unused)]
fn main() {
    use std::thread;
    
    let handler = thread::spawn(|| {
        // thread code
    });
    
    handler.join().unwrap();
}
