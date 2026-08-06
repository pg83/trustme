// Extracted from library/std/src/thread/scoped.rs:271
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::thread;
        
        thread::scope(|s| {
            let t = s.spawn(|| {
                println!("hello");
            });
            println!("thread id: {:?}", t.thread().id());
        });
        Ok(())
    }
    doctest().unwrap();
}
