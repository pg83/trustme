// Extracted from library/core/src/slice/iter.rs:290
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // First, we need a slice to call the `iter_mut` method on:
        let slice = &mut [1, 2, 3];
        
        // Then we call `iter_mut` on the slice to get the `IterMut` iterator:
        let mut iter = slice.iter_mut();
        // Here `as_slice` still returns the whole slice, so this prints "[1, 2, 3]":
        println!("{:?}", iter.as_slice());
        
        // Now, we call the `next` method to remove the first element from the iterator
        // and increment its value:
        *iter.next().unwrap() += 1;
        // Here the iterator does not contain the first element of the slice any more,
        // so `as_slice` only returns the last two elements of the slice,
        // and so this prints "[2, 3]":
        println!("{:?}", iter.as_slice());
        
        // The underlying slice still contains three elements, but its first element
        // was increased by 1, so this prints "[2, 2, 3]":
        println!("{:?}", slice);
        Ok(())
    }
    doctest().unwrap();
}
