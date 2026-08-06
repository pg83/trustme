// Extracted from library/alloc/src/boxed/convert.rs:265
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let boxed: Box<[u8]> = Box::from([4, 2]);
        println!("{boxed:?}");
        Ok(())
    }
    doctest().unwrap();
}
