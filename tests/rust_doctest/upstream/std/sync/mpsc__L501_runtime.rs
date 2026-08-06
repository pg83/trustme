// Extracted from library/std/src/sync/mpsc.rs:501
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::mpsc::channel;
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
