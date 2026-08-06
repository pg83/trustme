// Extracted from library/alloc/src/sync.rs:237
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::Arc;
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::thread;
        
        let val = Arc::new(AtomicUsize::new(5));
        
        for _ in 0..10 {
            let val = Arc::clone(&val);
        
            thread::spawn(move || {
                let v = val.fetch_add(1, Ordering::Relaxed);
                println!("{v:?}");
            });
        }
        Ok(())
    }
    doctest().unwrap();
}
