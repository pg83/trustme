// Extracted from library/alloc/src/vec/mod.rs:2540
#![allow(unused)]
#![feature(vec_push_within_capacity)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::collections::TryReserveError;
        fn from_iter_fallible<T>(iter: impl Iterator<Item=T>) -> Result<Vec<T>, TryReserveError> {
            let mut vec = Vec::new();
            for value in iter {
                if let Err(value) = vec.push_within_capacity(value) {
                    vec.try_reserve(1)?;
                    // this cannot fail, the previous line either returned or added at least 1 free slot
                    let _ = vec.push_within_capacity(value);
                }
            }
            Ok(vec)
        }
        assert_eq!(from_iter_fallible(0..100), Ok(Vec::from_iter(0..100)));
        Ok(())
    }
    doctest().unwrap();
}
