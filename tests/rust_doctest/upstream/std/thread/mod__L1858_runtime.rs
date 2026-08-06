// Extracted from library/std/src/thread/mod.rs:1858
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::thread;
        
        let builder = thread::Builder::new();
        
        let join_handle: thread::JoinHandle<_> = builder.spawn(|| {
            // some work here
        }).unwrap();
        
        let thread = join_handle.thread();
        println!("thread id: {:?}", thread.id());
        Ok(())
    }
    doctest().unwrap();
}
