// Extracted from library/alloc/src/sync.rs:218
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::Arc;
        use std::thread;
        
        let five = Arc::new(5);
        
        for _ in 0..10 {
            let five = Arc::clone(&five);
        
            thread::spawn(move || {
                println!("{five:?}");
            });
        }
        Ok(())
    }
    doctest().unwrap();
}
