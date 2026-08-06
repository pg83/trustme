// Extracted from library/core/src/slice/iter.rs:258
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // First, we need a slice to call the `iter_mut` method on:
        let mut slice = &mut [1, 2, 3];
        
        // Then we call `iter_mut` on the slice to get the `IterMut` struct:
        let mut iter = slice.iter_mut();
        // Now, we call the `next` method to remove the first element of the iterator,
        // unwrap and dereference what we get from `next` and increase its value by 1:
        *iter.next().unwrap() += 1;
        // Here the iterator does not contain the first element of the slice any more,
        // so `into_slice` only returns the last two elements of the slice,
        // and so this prints "[2, 3]":
        println!("{:?}", iter.into_slice());
        // The underlying slice still contains three elements, but its first element
        // was increased by 1, so this prints "[2, 2, 3]":
        println!("{:?}", slice);
        Ok(())
    }
    doctest().unwrap();
}
