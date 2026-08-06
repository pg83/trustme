// Extracted from library/std/src/thread/mod.rs:238
#![allow(unused)]
fn main() {
    use std::thread;
    
    let builder = thread::Builder::new();
    
    let handler = builder.spawn(|| {
        // thread code
    }).unwrap();
    
    handler.join().unwrap();
}
