// Extracted from library/alloc/src/boxed/convert.rs:122
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // create a &mut [u8] which will be used to create a Box<[u8]>
        let mut array = [104, 101, 108, 108, 111];
        let slice: &mut [u8] = &mut array;
        let boxed_slice: Box<[u8]> = Box::from(slice);
        
        println!("{boxed_slice:?}");
        Ok(())
    }
    doctest().unwrap();
}
