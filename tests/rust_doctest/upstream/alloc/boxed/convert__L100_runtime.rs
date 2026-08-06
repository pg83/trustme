// Extracted from library/alloc/src/boxed/convert.rs:100
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // create a &[u8] which will be used to create a Box<[u8]>
        let slice: &[u8] = &[104, 101, 108, 108, 111];
        let boxed_slice: Box<[u8]> = Box::from(slice);
        
        println!("{boxed_slice:?}");
        Ok(())
    }
    doctest().unwrap();
}
