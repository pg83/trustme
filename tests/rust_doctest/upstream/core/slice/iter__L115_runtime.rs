// Extracted from library/core/src/slice/iter.rs:115
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // First, we need a slice to call the `iter` method on:
        let slice = &[1, 2, 3];
        
        // Then we call `iter` on the slice to get the `Iter` iterator:
        let mut iter = slice.iter();
        // Here `as_slice` still returns the whole slice, so this prints "[1, 2, 3]":
        println!("{:?}", iter.as_slice());
        
        // Now, we call the `next` method to remove the first element from the iterator:
        iter.next();
        // Here the iterator does not contain the first element of the slice any more,
        // so `as_slice` only returns the last two elements of the slice,
        // and so this prints "[2, 3]":
        println!("{:?}", iter.as_slice());
        
        // The underlying slice has not been modified and still contains three elements,
        // so this prints "[1, 2, 3]":
        println!("{:?}", slice);
        Ok(())
    }
    doctest().unwrap();
}
