// Extracted from library/std/src/keyword_docs.rs:1011
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let data = vec![1, 2, 3];
        let closure = move || println!("captured {data:?} by value");
        
        // data is no longer available, it is owned by the closure
        Ok(())
    }
    doctest().unwrap();
}
