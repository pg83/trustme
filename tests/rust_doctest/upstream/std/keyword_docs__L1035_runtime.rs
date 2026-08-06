// Extracted from library/std/src/keyword_docs.rs:1035
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let data = vec![1, 2, 3];
        
        std::thread::spawn(move || {
            println!("captured {data:?} by value")
        }).join().unwrap();
        
        // data was moved to the spawned thread, so we cannot use it here
        Ok(())
    }
    doctest().unwrap();
}
