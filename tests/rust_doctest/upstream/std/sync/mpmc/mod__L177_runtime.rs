// Extracted from library/std/src/sync/mpmc/mod.rs:177
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::sync::mpmc::channel;
        use std::thread;
        
        let (sender, receiver) = channel();
        
        // Spawn off an expensive computation
        thread::spawn(move || {
          fn expensive_computation() {}
            sender.send(expensive_computation()).unwrap();
        });
        
        // Do some useful work for a while
        
        // Let's see what that answer was
        println!("{:?}", receiver.recv().unwrap());
        Ok(())
    }
    doctest().unwrap();
}
