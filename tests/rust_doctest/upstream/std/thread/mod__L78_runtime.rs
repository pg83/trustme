// Extracted from library/std/src/thread/mod.rs:78
#![allow(unused)]
#![allow(unused_must_use)]
fn main() {
    use std::thread;
    
    thread::Builder::new().name("thread1".to_string()).spawn(move || {
        println!("Hello, world!");
    });
}
