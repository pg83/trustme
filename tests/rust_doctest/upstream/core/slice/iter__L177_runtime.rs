// Extracted from library/core/src/slice/iter.rs:177
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        // First, we need a slice to call the `iter_mut` method on:
        let slice = &mut [1, 2, 3];
        
        // Then we call `iter_mut` on the slice to get the `IterMut` iterator,
        // iterate over it and increment each element value:
        for element in slice.iter_mut() {
            *element += 1;
        }
        
        // We now have "[2, 3, 4]":
        println!("{slice:?}");
        Ok(())
    }
    doctest().unwrap();
}
